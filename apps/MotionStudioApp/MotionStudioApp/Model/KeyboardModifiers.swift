//
//  KeyboardModifiers.swift
//  MotionStudioApp
//
//  Modifier key state for AE-style canvas / timeline interactions.
//

import GameController
import UIKit

enum KeyboardModifiers {
    static var shiftPressed: Bool {
        keyPressed(.leftShift) || keyPressed(.rightShift)
    }

    static var alternatePressed: Bool {
        keyPressed(.leftAlt) || keyPressed(.rightAlt)
    }

    private static func keyPressed(_ code: GCKeyCode) -> Bool {
        GCKeyboard.coalesced?.keyboardInput?.button(forKeyCode: code)?.isPressed == true
    }
}
