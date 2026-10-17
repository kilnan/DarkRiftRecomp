// Draws the project's original app icon (a glowing rift on a dark rounded square).
// usage: swift scripts/make_icon.swift icons/512.png
import AppKit

let size = 512
let rep = NSBitmapImageRep(bitmapDataPlanes: nil, pixelsWide: size, pixelsHigh: size, bitsPerSample: 8,
                           samplesPerPixel: 4, hasAlpha: true, isPlanar: false, colorSpaceName: .deviceRGB,
                           bytesPerRow: 0, bitsPerPixel: 0)!
NSGraphicsContext.saveGraphicsState()
NSGraphicsContext.current = NSGraphicsContext(bitmapImageRep: rep)
let ctx = NSGraphicsContext.current!.cgContext

let bg = CGPath(roundedRect: CGRect(x: 16, y: 16, width: 480, height: 480), cornerWidth: 96, cornerHeight: 96, transform: nil)
ctx.addPath(bg)
ctx.clip()
let colors = [CGColor(red: 0.10, green: 0.04, blue: 0.16, alpha: 1), CGColor(red: 0.02, green: 0.01, blue: 0.04, alpha: 1)] as CFArray
let grad = CGGradient(colorsSpace: CGColorSpaceCreateDeviceRGB(), colors: colors, locations: [0, 1])!
ctx.drawRadialGradient(grad, startCenter: CGPoint(x: 256, y: 256), startRadius: 0, endCenter: CGPoint(x: 256, y: 256), endRadius: 340, options: [])

// Jagged rift running diagonally.
let rift = CGMutablePath()
let pts: [(CGFloat, CGFloat)] = [(150, 470), (232, 330), (196, 300), (300, 170), (268, 150), (360, 40), (330, 168), (366, 186), (262, 318), (300, 340)]
rift.move(to: CGPoint(x: pts[0].0, y: pts[0].1))
for p in pts.dropFirst() { rift.addLine(to: CGPoint(x: p.0, y: p.1)) }
rift.closeSubpath()
ctx.setShadow(offset: .zero, blur: 40, color: CGColor(red: 0.75, green: 0.35, blue: 1.0, alpha: 1))
ctx.addPath(rift)
ctx.setFillColor(CGColor(red: 0.90, green: 0.75, blue: 1.0, alpha: 1))
ctx.fillPath()
ctx.setShadow(offset: .zero, blur: 0, color: nil)
ctx.addPath(rift)
ctx.setStrokeColor(CGColor(red: 1, green: 1, blue: 1, alpha: 0.85))
ctx.setLineWidth(4)
ctx.strokePath()

NSGraphicsContext.restoreGraphicsState()
try! rep.representation(using: .png, properties: [:])!.write(to: URL(fileURLWithPath: CommandLine.arguments[1]))
