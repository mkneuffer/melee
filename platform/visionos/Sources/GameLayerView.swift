import QuartzCore
import SwiftUI
import UIKit

/// The game's flat view: a CAMetalLayer Aurora presents to.
struct GameLayerView: UIViewRepresentable {
    let host: GameHost

    func makeUIView(context: Context) -> MetalLayerView {
        let view = MetalLayerView()
        view.onLayout = { [host] layer in
            // The first layout gives the layer its size; start the game on it
            host.start(layer: layer)
            host.layerResized(layer)
        }
        return view
    }

    func updateUIView(_ view: MetalLayerView, context: Context) {}
}

final class MetalLayerView: UIView {
    override class var layerClass: AnyClass { CAMetalLayer.self }

    var metalLayer: CAMetalLayer { layer as! CAMetalLayer }
    var onLayout: ((CAMetalLayer) -> Void)?

    override init(frame: CGRect) {
        super.init(frame: frame)
        metalLayer.pixelFormat = .bgra8Unorm
        metalLayer.framebufferOnly = true
        isUserInteractionEnabled = false
    }

    required init?(coder: NSCoder) { fatalError() }

    override func layoutSubviews() {
        super.layoutSubviews()
        let scale = traitCollection.displayScale
        metalLayer.contentsScale = scale
        let size = CGSize(width: bounds.width * scale, height: bounds.height * scale)
        guard size.width > 0, size.height > 0, size != metalLayer.drawableSize else { return }
        metalLayer.drawableSize = size
        onLayout?(metalLayer)
    }
}
