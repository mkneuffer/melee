import CompositorServices
import SwiftUI

/// The app: a window with the game's flat view (its menus, and in a match its
/// HUD), and an immersive space where the match stands on the player's table.
@main
struct DioramaApp: App {
    @State private var host = GameHost()
    @State private var immersion: ImmersionStyle = .mixed

    var body: some Scene {
        WindowGroup(id: "Game") {
            GameWindow()
                .environment(host)
        }
        .defaultSize(width: 1280, height: 960)

        ImmersiveSpace(id: SpatialRenderer.spaceID) {
            CompositorLayer(configuration: SpatialRenderer.Configuration()) { layerRenderer in
                SpatialRenderer.start(layerRenderer, host: host)
            }
        }
        .immersionStyle(selection: $immersion, in: .mixed)
    }
}

/// The game's window: importing the disc, then the game's flat view.
struct GameWindow: View {
    @Environment(GameHost.self) private var host
    @Environment(\.openImmersiveSpace) private var openImmersiveSpace
    @Environment(\.dismissImmersiveSpace) private var dismissImmersiveSpace
    @Environment(\.scenePhase) private var scenePhase
    @State private var importing = false
    @State private var spaceOpen = false

    var body: some View {
        @Bindable var host = host
        Group {
            switch host.state {
            case .needsDisc:
                ContentUnavailableView {
                    Label("Choose your disc image", systemImage: "opticaldisc")
                } description: {
                    Text("An image (ISO, GCM, RVZ) of your own copy of the game, version 1.02 (GALE01).")
                } actions: {
                    Button("Choose…") { importing = true }
                }
            case .running:
                GameLayerView(host: host)
                    .aspectRatio(4 / 3, contentMode: .fit)
                    .overlay(alignment: .bottom) { controls }
            case .failed(let message):
                ContentUnavailableView("The game stopped", systemImage: "exclamationmark.triangle",
                                       description: Text(message))
            }
        }
        .fileImporter(isPresented: $importing, allowedContentTypes: [.data]) { result in
            if case .success(let url) = result {
                host.importDisc(from: url)
            }
        }
        .onChange(of: host.inMatch) { _, inMatch in
            guard host.spatialWanted else { return }
            Task { await setSpace(open: inMatch) }
        }
        .onChange(of: host.spatialWanted) { _, wanted in
            Task { await setSpace(open: wanted && host.inMatch) }
        }
        .onChange(of: scenePhase) { _, phase in
            host.setPaused(phase == .background)
        }
    }

    private var controls: some View {
        @Bindable var host = host
        return HStack(spacing: 24) {
            Toggle("On the table", isOn: $host.spatialWanted)
                .toggleStyle(.button)
            if spaceOpen {
                Text("Pinch a table to move the stage")
                    .font(.footnote)
                    .foregroundStyle(.secondary)
                Slider(value: $host.stageWidth, in: 0.3...1.5) {
                    Text("Size")
                } minimumValueLabel: {
                    Image(systemName: "minus.magnifyingglass")
                } maximumValueLabel: {
                    Image(systemName: "plus.magnifyingglass")
                }
                .frame(width: 240)
            }
            Text(host.controllerSummary)
                .font(.footnote)
                .foregroundStyle(.secondary)
        }
        .padding()
        .glassBackgroundEffect()
        .padding()
    }

    private func setSpace(open: Bool) async {
        guard open != spaceOpen else { return }
        if open {
            if case .opened = await openImmersiveSpace(id: SpatialRenderer.spaceID) {
                spaceOpen = true
                host.setSpatial(true)
            }
        } else {
            host.setSpatial(false)
            await dismissImmersiveSpace()
            spaceOpen = false
        }
    }
}
