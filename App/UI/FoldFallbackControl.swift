import SwiftUI

/// Stands in for the hinge on iPhones without one. Left is folded, right is flat.
struct FoldFallbackControl: View {
  @Binding var angle: Double

  var body: some View {
    VStack(spacing: 10) {
      Text("Made for iPhone Duo. Drag to fold.")
        .font(.footnote)
        .foregroundStyle(.white.opacity(0.7))
      Slider(value: $angle, in: DHCore.steepDegrees...DHCore.flatDegrees) {
        Text("Fold")
      } minimumValueLabel: {
        Image(systemName: "book.closed")
      } maximumValueLabel: {
        Image(systemName: "book")
      }
      .tint(.white.opacity(0.6))
      .foregroundStyle(.white.opacity(0.6))
      .accessibilityValue("\(Int(angle)) degrees")
    }
    .padding(.horizontal, 28)
    .padding(.vertical, 16)
    .background(.black.opacity(0.35), in: RoundedRectangle(cornerRadius: 20))
    .padding(.horizontal, 16)
    .padding(.bottom, 12)
  }
}
