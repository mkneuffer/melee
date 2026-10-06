import Foundation
import GameController
import simd

/// The player's controllers, as one GameCube controller on port 1.
///
/// Made for PS VR2 Sense controllers, a controller in each hand, which the
/// game controller framework gives as two controllers; each is read as the
/// half of a GameCube controller its hand holds:
///
/// | Sense, left hand | GameCube  | Sense, right hand | GameCube |
/// |------------------|-----------|-------------------|----------|
/// | stick            | stick     | stick             | C-stick  |
/// | L2 (trigger)     | L         | R2 (trigger)      | R        |
/// | L1 (grip)        | D-pad*    | R1 (grip)         | Z        |
/// | square           | X         | cross             | A        |
/// | triangle         | Y         | circle            | B        |
/// | create           | Start     | options           | Start    |
///
/// *While L1 is held, the stick is the D-pad (taunts, Training Mode's menus)
/// instead.
///
/// A gamepad with both halves (DualSense, Xbox, the like) maps the same way,
/// its own D-pad as the D-pad.
final class InputMapper {
    /// Which hand a controller is held in, by the face buttons it has.
    private enum Hand {
        case left, right, both
    }

    /// The controllers' state changed: what to show the player.
    var onChange: ((String) -> Void)?

    private var timer: Timer?
    private var controllers: [GCController] = []

    /// How far a trigger goes before it clicks: the digital L and R.
    private static let triggerClick: Float = 0.92
    private static let stickDeadZone: Float = 0.04

    func start() {
        guard timer == nil else { return }
        GCController.shouldMonitorBackgroundEvents = true
        let center = NotificationCenter.default
        center.addObserver(forName: .GCControllerDidConnect, object: nil, queue: .main) { [weak self] _ in
            self?.refresh()
        }
        center.addObserver(forName: .GCControllerDidDisconnect, object: nil, queue: .main) { [weak self] _ in
            self?.refresh()
        }
        GCController.startWirelessControllerDiscovery()
        refresh()
        // The retrace samples at 60 Hz; set the state well inside that
        timer = Timer.scheduledTimer(withTimeInterval: 1.0 / 240.0, repeats: true) { [weak self] _ in
            self?.poll()
        }
    }

    private func refresh() {
        controllers = GCController.controllers()
        let names = controllers.map { controller -> String in
            let name = controller.vendorName ?? "Controller"
            switch Self.hand(of: controller) {
            case .left: return "\(name) (left)"
            case .right: return "\(name) (right)"
            case .both: return name
            }
        }
        onChange?(names.isEmpty ? "No controllers" : names.joined(separator: " + "))
    }

    private static func hand(of controller: GCController) -> Hand {
        let buttons = controller.physicalInputProfile.buttons
        let right = buttons[GCInputButtonA] != nil || buttons[GCInputButtonB] != nil
        let left = buttons[GCInputButtonX] != nil || buttons[GCInputButtonY] != nil
        if right && !left { return .right }
        if left && !right { return .left }
        return .both
    }

    private struct Half {
        var stick = SIMD2<Float>.zero
        var trigger: Float = 0
        var shoulder = false
        var face1 = false // A on the right, X on the left
        var face2 = false // B on the right, Y on the left
        var start = false
    }

    private func poll() {
        var left = Half()
        var right = Half()
        var dpad = SIMD2<Float>.zero

        for controller in controllers {
            let profile = controller.physicalInputProfile
            let buttons = profile.buttons
            let dpads = profile.dpads
            func pressed(_ name: String) -> Bool { buttons[name]?.isPressed ?? false }
            func value(_ name: String) -> Float { buttons[name]?.value ?? 0 }
            func stick(_ name: String) -> SIMD2<Float> {
                guard let pad = dpads[name] else { return .zero }
                return SIMD2(pad.xAxis.value, pad.yAxis.value)
            }
            /// Whichever stick, trigger or shoulder a one-handed controller
            /// has: a half needn't name its elements for its hand.
            func any(_ a: SIMD2<Float>, _ b: SIMD2<Float>) -> SIMD2<Float> {
                simd_length_squared(a) >= simd_length_squared(b) ? a : b
            }
            let start = pressed(GCInputButtonMenu) || pressed(GCInputButtonOptions)

            switch Self.hand(of: controller) {
            case .left:
                left.stick = any(left.stick, any(stick(GCInputLeftThumbstick), stick(GCInputRightThumbstick)))
                left.trigger = max(left.trigger, max(value(GCInputLeftTrigger), value(GCInputRightTrigger)))
                left.shoulder = left.shoulder || pressed(GCInputLeftShoulder) || pressed(GCInputRightShoulder)
                left.face1 = left.face1 || pressed(GCInputButtonX)
                left.face2 = left.face2 || pressed(GCInputButtonY)
                left.start = left.start || start
            case .right:
                right.stick = any(right.stick, any(stick(GCInputRightThumbstick), stick(GCInputLeftThumbstick)))
                right.trigger = max(right.trigger, max(value(GCInputRightTrigger), value(GCInputLeftTrigger)))
                right.shoulder = right.shoulder || pressed(GCInputRightShoulder) || pressed(GCInputLeftShoulder)
                right.face1 = right.face1 || pressed(GCInputButtonA)
                right.face2 = right.face2 || pressed(GCInputButtonB)
                right.start = right.start || start
            case .both:
                left.stick = any(left.stick, stick(GCInputLeftThumbstick))
                right.stick = any(right.stick, stick(GCInputRightThumbstick))
                left.trigger = max(left.trigger, value(GCInputLeftTrigger))
                right.trigger = max(right.trigger, value(GCInputRightTrigger))
                left.shoulder = left.shoulder || pressed(GCInputLeftShoulder)
                right.shoulder = right.shoulder || pressed(GCInputRightShoulder)
                left.face1 = left.face1 || pressed(GCInputButtonX)
                left.face2 = left.face2 || pressed(GCInputButtonY)
                right.face1 = right.face1 || pressed(GCInputButtonA)
                right.face2 = right.face2 || pressed(GCInputButtonB)
                left.start = left.start || start
                dpad = any(dpad, stick(GCInputDirectionPad))
            }
        }

        var pad = PortPad()
        pad.connected = !controllers.isEmpty
        var buttons: Int32 = 0
        if right.face1 { buttons |= Int32(PORT_PAD_A) }
        if right.face2 { buttons |= Int32(PORT_PAD_B) }
        if left.face1 { buttons |= Int32(PORT_PAD_X) }
        if left.face2 { buttons |= Int32(PORT_PAD_Y) }
        if right.shoulder { buttons |= Int32(PORT_PAD_Z) }
        if left.start || right.start { buttons |= Int32(PORT_PAD_START) }
        if left.trigger >= Self.triggerClick { buttons |= Int32(PORT_PAD_L) }
        if right.trigger >= Self.triggerClick { buttons |= Int32(PORT_PAD_R) }

        // L1 turns the stick into the D-pad
        var stick = left.stick
        if left.shoulder {
            dpad = any(dpad, stick)
            stick = .zero
        }
        if dpad.x < -0.5 { buttons |= Int32(PORT_PAD_LEFT) }
        if dpad.x > 0.5 { buttons |= Int32(PORT_PAD_RIGHT) }
        if dpad.y < -0.5 { buttons |= Int32(PORT_PAD_DOWN) }
        if dpad.y > 0.5 { buttons |= Int32(PORT_PAD_UP) }

        pad.buttons = UInt16(truncatingIfNeeded: buttons)
        (pad.stick_x, pad.stick_y) = Self.axes(stick)
        (pad.cstick_x, pad.cstick_y) = Self.axes(right.stick)
        pad.trigger_l = Self.trigger(left.trigger)
        pad.trigger_r = Self.trigger(right.trigger)
        port_pad_set(0, &pad)
    }

    /// A stick, -1 to 1, as a GameCube controller's, -100 to 100.
    private static func axes(_ v: SIMD2<Float>) -> (Int8, Int8) {
        var v = v
        let length = simd_length(v)
        if length < stickDeadZone { return (0, 0) }
        if length > 1 { v /= length }
        return (Int8((v.x * 100).rounded()), Int8((v.y * 100).rounded()))
    }

    private static func trigger(_ v: Float) -> UInt8 {
        UInt8((min(max(v, 0), 1) * 255).rounded())
    }
}
