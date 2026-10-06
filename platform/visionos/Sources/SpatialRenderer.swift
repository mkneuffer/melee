import ARKit
import CompositorServices
import IOSurface
import Metal
import QuartzCore
import simd
import Spatial
import SwiftUI

/// The match on the player's table: the immersive space's renderer.
///
/// The compositor runs at the display's rate (90 Hz); the game draws at its
/// own (60 Hz). Each compositor frame, this gives the game the eyes' latest
/// poses to draw its next frame from, into a set of IOSurfaces, and shows the
/// latest set the game finished, telling the compositor the pose it was drawn
/// from (`drawable.deviceAnchor`), which it reprojects to the present one.
enum SpatialRenderer {
    static let spaceID = "Diorama"

    struct Configuration: CompositorLayerConfiguration {
        func makeConfiguration(capabilities: LayerRenderer.Capabilities,
                               configuration: inout LayerRenderer.Configuration) {
            configuration.colorFormat = .bgra8Unorm_srgb
            configuration.depthFormat = .depth32Float
            // The game's images are small; foveating them gains nothing
            configuration.isFoveationEnabled = false
            let layouts = capabilities.supportedLayouts(options: [])
            configuration.layout = layouts.contains(.layered) ? .layered : .dedicated
        }
    }

    static func start(_ layerRenderer: LayerRenderer, host: GameHost) {
        let renderer = Renderer(layerRenderer, host: host)
        let thread = Thread { renderer.run() }
        thread.name = "Spatial"
        thread.qualityOfService = .userInteractive
        thread.start()
    }
}

private final class Renderer {
    /// The pixels of each eye's image the game draws.
    private static let eyeSize = 1440
    /// Sets of eye images: one shown, up to two with the game, one spare.
    private static let setCount = 4
    /// How high off the table the stage's floor stands, in meters.
    private static let tableClearance: Float = 0.005
    /// Where the stage stands before the player puts it somewhere: in front
    /// of them, at a table's height.
    private static let defaultDistance: Float = 0.9
    private static let defaultHeight: Float = 0.75

    private struct EyeSet {
        var surfaces: [IOSurface]
        var textures: [MTLTexture]
    }

    private struct Rendering {
        var set: Int
        var anchor: DeviceAnchor
    }

    private let layerRenderer: LayerRenderer
    private let host: GameHost
    private let device: MTLDevice
    private let queue: MTLCommandQueue
    private let pipeline: MTLRenderPipelineState
    private let depthState: MTLDepthStencilState

    private let session = ARKitSession()
    private let world = WorldTrackingProvider()
    private let planes = PlaneDetectionProvider(alignments: [.horizontal])
    private let lock = NSLock()
    /// The tables' heights and extents, as ARKit finds them.
    private var tables: [UUID: PlaneAnchor] = [:]

    private var sets: [EyeSet] = []
    private var free: [Int]
    private var rendering: [UInt64: Rendering] = [:]
    private var shown: (id: UInt64, rendering: Rendering)?
    private var nextID: UInt64 = 1

    /// Where the stage stands: a point on the table, and which way it faces.
    private var stagePoint: SIMD3<Float>?
    private var stageYaw: Float = 0

    init(_ layerRenderer: LayerRenderer, host: GameHost) {
        self.layerRenderer = layerRenderer
        self.host = host
        device = layerRenderer.device
        queue = device.makeCommandQueue()!

        let library = device.makeDefaultLibrary()!
        let descriptor = MTLRenderPipelineDescriptor()
        descriptor.vertexFunction = library.makeFunction(name: "composite_vertex")
        descriptor.fragmentFunction = library.makeFunction(name: "composite_fragment")
        descriptor.colorAttachments[0].pixelFormat = layerRenderer.configuration.colorFormat
        descriptor.depthAttachmentPixelFormat = layerRenderer.configuration.depthFormat
        descriptor.inputPrimitiveTopology = .triangle
        pipeline = try! device.makeRenderPipelineState(descriptor: descriptor)

        let depth = MTLDepthStencilDescriptor()
        depth.depthCompareFunction = .always
        depth.isDepthWriteEnabled = true
        depthState = device.makeDepthStencilState(descriptor: depth)!

        free = Array(0..<Self.setCount)
        for _ in 0..<Self.setCount {
            sets.append(makeEyeSet())
        }
    }

    private func makeEyeSet() -> EyeSet {
        var surfaces: [IOSurface] = []
        var textures: [MTLTexture] = []
        for _ in 0..<2 {
            let surface = IOSurface(properties: [
                .width: Self.eyeSize,
                .height: Self.eyeSize,
                .bytesPerElement: 4,
                .pixelFormat: 0x4247_5241, // 'BGRA'
            ])!
            let descriptor = MTLTextureDescriptor.texture2DDescriptor(
                pixelFormat: .bgra8Unorm, width: Self.eyeSize, height: Self.eyeSize, mipmapped: false)
            descriptor.usage = .shaderRead
            descriptor.storageMode = .shared
            surfaces.append(surface)
            textures.append(device.makeTexture(descriptor: descriptor, iosurface: surface, plane: 0)!)
        }
        return EyeSet(surfaces: surfaces, textures: textures)
    }

    // MARK: - The loop

    func run() {
        Task { await runSession() }
        layerRenderer.onSpatialEvent = { [weak self] events in
            self?.handle(events)
        }

        while true {
            switch layerRenderer.state {
            case .paused:
                layerRenderer.waitUntilRunning()
                continue
            case .invalidated:
                port_spatial_set_enabled(false)
                session.stop()
                return
            default:
                break
            }
            autoreleasepool { renderFrame() }
        }
    }

    private func runSession() async {
        _ = await session.requestAuthorization(for: [.worldSensing])
        var providers: [any DataProvider] = [world]
        if PlaneDetectionProvider.isSupported {
            providers.append(planes)
        }
        do {
            try await session.run(providers)
        } catch {
            print("Spatial: ARKit didn't start: \(error)")
            return
        }
        guard PlaneDetectionProvider.isSupported else { return }
        for await update in planes.anchorUpdates {
            let anchor = update.anchor
            lock.lock()
            if update.event == .removed || anchor.classification != .table {
                tables[anchor.id] = nil
            } else {
                tables[anchor.id] = anchor
            }
            lock.unlock()
        }
    }

    private func renderFrame() {
        guard let frame = layerRenderer.queryNextFrame() else { return }
        frame.startUpdate()
        frame.endUpdate()
        guard let timing = frame.predictTiming() else { return }
        LayerRenderer.Clock().wait(until: timing.optimalInputTime)

        frame.startSubmission()
        guard let drawable = frame.queryDrawable() else {
            frame.endSubmission()
            return
        }
        let time = LayerRenderer.Clock.Instant.epoch.duration(to: drawable.frameTiming.presentationTime)
            .timeInterval
        let anchor = world.queryDeviceAnchor(atTimestamp: time)

        collectRendered()
        if let anchor {
            placeStageIfNeeded(anchor)
            submit(drawable: drawable, anchor: anchor)
        }

        // Show the latest eyes from the pose they were drawn from
        drawable.deviceAnchor = shown?.rendering.anchor ?? anchor
        drawable.depthRange = SIMD2(Self.farZ, Self.nearZ)
        let commands = queue.makeCommandBuffer()!
        composite(into: drawable, commands: commands)
        drawable.encodePresent(commandBuffer: commands)
        commands.commit()
        frame.endSubmission()
    }

    /// Take the latest eyes the game finished; free the sets it skipped.
    private func collectRendered() {
        let after = shown?.id ?? 0
        let id = port_spatial_wait_rendered(after, 0)
        guard id > after, let done = rendering[id] else { return }
        if let old = shown {
            free.append(old.rendering.set)
        }
        shown = (id, done)
        rendering[id] = nil
        for (older, skipped) in rendering where older < id {
            free.append(skipped.set)
            rendering[older] = nil
        }
    }

    // MARK: - The game's eyes

    private func submit(drawable: LayerRenderer.Drawable, anchor: DeviceAnchor) {
        guard rendering.count < 2, let set = free.popLast() else { return }

        port_spatial_set_stage(stageMatrix())

        var frame = PortSpatialFrame()
        frame.id = nextID
        frame.width = UInt32(Self.eyeSize)
        frame.height = UInt32(Self.eyeSize)
        let originFromDevice = anchor.originFromAnchorTransform
        let count = min(drawable.views.count, 2)
        frame.eye_count = UInt32(count)

        withUnsafeMutablePointer(to: &frame.eyes) { eyes in
            eyes.withMemoryRebound(to: PortEye.self, capacity: 2) { eyes in
                for i in 0..<count {
                    let view = drawable.views[i]
                    let eyeFromOrigin = (originFromDevice * view.transform).inverse
                    eyes[i].view = Self.portMatrix(eyeFromOrigin)
                    // left, right, top, bottom, as magnitudes
                    let t = view.tangents
                    eyes[i].tan_left = -t[0]
                    eyes[i].tan_right = t[1]
                    eyes[i].tan_top = t[2]
                    eyes[i].tan_bottom = -t[3]
                }
            }
        }
        frame.surfaces.0 = Unmanaged.passUnretained(sets[set].surfaces[0]).toOpaque()
        frame.surfaces.1 = Unmanaged.passUnretained(sets[set].surfaces[1]).toOpaque()

        port_spatial_submit(&frame)
        rendering[nextID] = Rendering(set: set, anchor: anchor)
        nextID += 1
    }

    private func composite(into drawable: LayerRenderer.Drawable, commands: MTLCommandBuffer) {
        let layered = layerRenderer.configuration.layout == .layered
        for (i, view) in drawable.views.enumerated() {
            let pass = MTLRenderPassDescriptor()
            pass.colorAttachments[0].texture = drawable.colorTextures[layered ? 0 : i]
            pass.colorAttachments[0].slice = layered ? i : 0
            pass.colorAttachments[0].loadAction = .clear
            pass.colorAttachments[0].clearColor = MTLClearColor(red: 0, green: 0, blue: 0, alpha: 0)
            pass.colorAttachments[0].storeAction = .store
            pass.depthAttachment.texture = drawable.depthTextures[layered ? 0 : i]
            pass.depthAttachment.slice = layered ? i : 0
            pass.depthAttachment.loadAction = .clear
            pass.depthAttachment.clearDepth = 0 // reverse-Z: far
            pass.depthAttachment.storeAction = .store

            let encoder = commands.makeRenderCommandEncoder(descriptor: pass)!
            let viewport = view.textureMap.viewport
            encoder.setViewport(viewport)
            if let shown, i < 2 {
                var uniforms = CompositeUniforms(
                    key: SIMD3(Float(PORT_KEY_R), Float(PORT_KEY_G), Float(PORT_KEY_B)) / 255,
                    depth: stageDepth(anchor: shown.rendering.anchor, view: view))
                encoder.setRenderPipelineState(pipeline)
                encoder.setDepthStencilState(depthState)
                encoder.setFragmentTexture(sets[shown.rendering.set].textures[i], index: 0)
                encoder.setFragmentBytes(&uniforms, length: MemoryLayout<CompositeUniforms>.stride, index: 0)
                encoder.drawPrimitives(type: .triangle, vertexStart: 0, vertexCount: 3)
            }
            encoder.endEncoding()
        }
    }

    /// Matches Composite.metal's.
    private struct CompositeUniforms {
        var key: SIMD3<Float>
        var depth: Float
    }

    /// The depth range the composite's depth is written in (reverse-Z): what
    /// the compositor reprojects the game's eyes by.
    private static let nearZ: Float = 0.1
    private static let farZ: Float = 100

    /// The stage's depth as a reverse-Z projection with an infinite far plane
    /// has it: near over distance. The game's images carry no depth of their
    /// own; the stage is small next to how far it is, so all of it at its
    /// center's depth is close enough for reprojection.
    private func stageDepth(anchor: DeviceAnchor, view: LayerRenderer.Drawable.View) -> Float {
        guard let point = stagePoint else { return 0 }
        let eyeFromOrigin = (anchor.originFromAnchorTransform * view.transform).inverse
        let p = eyeFromOrigin * SIMD4(point, 1)
        let distance = max(-p.z, Self.nearZ)
        return min(Self.nearZ / distance, 1)
    }

    // MARK: - The stage

    /// The game's world to the room's: the stage's floor on the table,
    /// facing where the player stood when they placed it, the size they
    /// chose.
    private func stageMatrix() -> PortMat4 {
        var center: (Float, Float, Float) = (0, 0, 0)
        var radius: Float = 1
        let known = withUnsafeMutablePointer(to: &center) { c in
            c.withMemoryRebound(to: Float.self, capacity: 3) { port_stage_bounds($0, &radius) }
        }
        let width = host.stageWidth
        let scale = known && radius > 0 ? width / (2 * radius) : width / 400
        let point = stagePoint ?? .zero

        let translate = simd_float4x4(translation: point + SIMD3(0, Self.tableClearance, 0))
        let rotate = simd_float4x4(simd_quatf(angle: stageYaw, axis: SIMD3(0, 1, 0)))
        let scaled = simd_float4x4(diagonal: SIMD4(scale, scale, scale, 1))
        let recenter = simd_float4x4(translation: SIMD3(-center.0, 0, -center.2))
        return Self.portMatrix(translate * rotate * scaled * recenter)
    }

    private func placeStageIfNeeded(_ anchor: DeviceAnchor) {
        guard stagePoint == nil else { return }
        let device = anchor.originFromAnchorTransform
        let position = SIMD3(device.columns.3.x, device.columns.3.y, device.columns.3.z)
        var forward = -SIMD3(device.columns.2.x, 0, device.columns.2.z)
        if simd_length(forward) < 1e-3 { forward = SIMD3(0, 0, -1) }
        forward = simd_normalize(forward)

        // The nearest table in front, else in front at a table's height
        var best: SIMD3<Float>?
        lock.lock()
        for table in tables.values {
            let t = table.originFromAnchorTransform.columns.3
            let p = SIMD3(t.x, t.y, t.z)
            if simd_dot(p - position, forward) > 0,
               best == nil || simd_distance(p, position) < simd_distance(best!, position) {
                best = p
            }
        }
        lock.unlock()
        let point = best ?? SIMD3(position.x, Self.defaultHeight, position.z) + forward * Self.defaultDistance
        place(at: point, facing: position)
    }

    private func place(at point: SIMD3<Float>, facing viewer: SIMD3<Float>) {
        stagePoint = point
        // The stage's front (+Z, where the game's camera looks from) to the
        // player
        let toViewer = viewer - point
        stageYaw = atan2(toViewer.x, toViewer.z)
    }

    /// Pinch somewhere on a table (or the floor's height a table would have)
    /// to move the stage there.
    private func handle(_ events: SpatialEventCollection) {
        for event in events where event.phase == .ended {
            guard let ray = event.selectionRay else { continue }
            let origin = SIMD3<Float>(Float(ray.origin.x), Float(ray.origin.y), Float(ray.origin.z))
            let direction = simd_normalize(SIMD3<Float>(
                Float(ray.direction.x), Float(ray.direction.y), Float(ray.direction.z)))
            guard direction.y < -0.05 else { continue }

            var heights: [Float] = []
            lock.lock()
            heights = tables.values.map { $0.originFromAnchorTransform.columns.3.y }
            lock.unlock()
            let height = heights.filter { $0 < origin.y }.max() ?? stagePoint?.y ?? Self.defaultHeight
            let t = (height - origin.y) / direction.y
            place(at: origin + direction * t, facing: origin)
        }
    }

    // MARK: - Matrices

    private static func portMatrix(_ m: simd_float4x4) -> PortMat4 {
        var out = PortMat4()
        withUnsafeMutablePointer(to: &out.m) { p in
            p.withMemoryRebound(to: Float.self, capacity: 16) { f in
                for c in 0..<4 {
                    for r in 0..<4 {
                        f[c * 4 + r] = m[c][r]
                    }
                }
            }
        }
        return out
    }
}

private extension simd_float4x4 {
    init(translation t: SIMD3<Float>) {
        self = matrix_identity_float4x4
        columns.3 = SIMD4(t, 1)
    }
}
