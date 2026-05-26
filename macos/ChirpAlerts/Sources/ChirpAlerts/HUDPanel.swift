import AppKit
import SwiftUI

struct HUDContent: View {
    let titles: [String]
    let onDismiss: () -> Void

    var body: some View {
        VStack(alignment: .leading, spacing: 10) {
            Text("⏰  REMINDER CHECK-IN")
                .font(.system(size: 16, weight: .heavy, design: .monospaced))
                .foregroundStyle(
                    LinearGradient(colors: [.red, .orange], startPoint: .leading, endPoint: .trailing)
                )
                .padding(.bottom, 4)

            if titles.isEmpty {
                Text("(No open reminders — you're clear!)")
                    .font(.system(size: 13, design: .monospaced))
                    .foregroundColor(.cyan)
            } else {
                ForEach(Array(titles.enumerated()), id: \.offset) { _, t in
                    Text("• " + t)
                        .font(.system(size: 13, weight: .semibold, design: .monospaced))
                        .foregroundColor(.white)
                }
            }

            HStack {
                Spacer()
                Button("Dismiss") {
                    onDismiss()
                }
                .keyboardShortcut(.defaultAction)
                .buttonStyle(.borderedProminent)
                .tint(.cyan)
            }
            .padding(.top, 8)
        }
        .padding(22)
        .frame(minWidth: 360, minHeight: 120)
        .background(
            RoundedRectangle(cornerRadius: 10)
                .fill(Color(red: 0.06, green: 0.07, blue: 0.12))
                .overlay(
                    RoundedRectangle(cornerRadius: 10)
                        .strokeBorder(Color.cyan.opacity(0.45), lineWidth: 1)
                )
        )
    }
}

@MainActor
final class HUDPresenter {
    private var panel: NSPanel?

    func show(titles: [String]) {
        dismiss()
        let root = HUDContent(titles: titles) { [weak self] in
            self?.dismiss()
        }
        let host = NSHostingController(rootView: root)
        host.preferredContentSize = NSSize(width: 440, height: 300)

        let panel = NSPanel(
            contentRect: NSRect(x: 0, y: 0, width: 440, height: 300),
            styleMask: [.titled, .closable, .fullSizeContentView],
            backing: .buffered,
            defer: false
        )
        panel.title = "CHIRP"
        panel.titlebarAppearsTransparent = true
        panel.isFloatingPanel = true
        panel.level = .floating
        panel.collectionBehavior = [.canJoinAllSpaces, .fullScreenAuxiliary]
        panel.hidesOnDeactivate = false
        panel.isReleasedWhenClosed = false
        panel.contentView = host.view
        panel.center()
        panel.makeKeyAndOrderFront(nil)
        NSApp.activate(ignoringOtherApps: true)
        self.panel = panel
    }

    func dismiss() {
        panel?.close()
        panel = nil
    }
}
