import Foundation
import Observation
import QuartzCore

/// The game, on its own thread, and what the app's views know of it.
@Observable
final class GameHost {
    enum State: Equatable {
        case needsDisc
        case running
        case failed(String)
    }

    private(set) var state: State
    /// Whether the game is in a match: when the stage goes on the table.
    private(set) var inMatch = false
    /// Whether the player wants the match on the table, or in the window.
    var spatialWanted = true
    /// How wide the stage stands on the table, in meters.
    var stageWidth: Float = 0.6
    private(set) var controllerSummary = "No controllers"

    @ObservationIgnored let input = InputMapper()
    @ObservationIgnored private var thread: Thread?
    @ObservationIgnored private var pollTimer: Timer?

    private static var support: URL {
        let url = FileManager.default.urls(for: .applicationSupportDirectory, in: .userDomainMask)[0]
        try? FileManager.default.createDirectory(at: url, withIntermediateDirectories: true)
        return url
    }

    /// The player's disc, copied into the app's own storage when they chose it.
    private static var discURL: URL { support.appendingPathComponent("disc") }

    init() {
        state = FileManager.default.fileExists(atPath: Self.discURL.path) ? .running : .needsDisc
        input.onChange = { [weak self] summary in self?.controllerSummary = summary }
        pollTimer = Timer.scheduledTimer(withTimeInterval: 0.25, repeats: true) { [weak self] _ in
            guard let self else { return }
            let now = port_in_match()
            if now != self.inMatch { self.inMatch = now }
        }
    }

    func importDisc(from url: URL) {
        let scoped = url.startAccessingSecurityScopedResource()
        defer { if scoped { url.stopAccessingSecurityScopedResource() } }
        do {
            try? FileManager.default.removeItem(at: Self.discURL)
            try FileManager.default.copyItem(at: url, to: Self.discURL)
            state = .running
        } catch {
            state = .failed("Couldn't copy the disc image: \(error.localizedDescription)")
        }
    }

    /// Start the game, presenting its flat view to `layer`. Once.
    func start(layer: CAMetalLayer) {
        guard thread == nil, state == .running else { return }
        let size = layer.drawableSize
        let scale = Float(layer.contentsScale)
        let disc = Self.discURL.path
        let user = Self.support.appendingPathComponent("user").path
        let cache = FileManager.default.urls(for: .cachesDirectory, in: .userDomainMask)[0].path
        try? FileManager.default.createDirectory(atPath: user, withIntermediateDirectories: true)
        let layerPointer = Unmanaged.passRetained(layer).toOpaque()

        let thread = Thread { [weak self] in
            let status = disc.withCString { discC in
                user.withCString { userC in
                    cache.withCString { cacheC in
                        var config = PortConfig(
                            disc_path: discC, user_path: userC, cache_path: cacheC,
                            metal_layer: layerPointer,
                            layer_width: UInt32(size.width), layer_height: UInt32(size.height),
                            layer_scale: scale)
                        return port_run(&config)
                    }
                }
            }
            Unmanaged<CAMetalLayer>.fromOpaque(layerPointer).release()
            DispatchQueue.main.async {
                self?.state = status == 0 ? .needsDisc : .failed("The game exited (\(status)).")
                self?.thread = nil
            }
        }
        thread.name = "Game"
        thread.qualityOfService = .userInteractive
        thread.stackSize = 16 << 20
        self.thread = thread
        thread.start()
        input.start()
    }

    func layerResized(_ layer: CAMetalLayer) {
        let size = layer.drawableSize
        port_set_layer_size(UInt32(size.width), UInt32(size.height), Float(layer.contentsScale))
    }

    func setPaused(_ paused: Bool) {
        port_set_paused(paused)
    }

    func setSpatial(_ on: Bool) {
        port_spatial_set_enabled(on)
    }
}
