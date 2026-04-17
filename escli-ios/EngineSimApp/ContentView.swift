import SwiftUI

struct GaugeView: View {
    let title: String
    let value: String
    let unit: String
    let color: Color

    var body: some View {
        VStack(alignment: .leading, spacing: 4) {
            Text(title)
                .font(.caption)
                .foregroundColor(.secondary)
            HStack(alignment: .firstTextBaseline, spacing: 2) {
                Text(value)
                    .font(.title2)
                    .fontWeight(.semibold)
                    .foregroundColor(color)
                Text(unit)
                    .font(.caption)
                    .foregroundColor(.secondary)
            }
        }
        .padding()
        .background(Color(.systemGray6))
        .cornerRadius(12)
    }
}

struct ContentView: View {
    @StateObject private var viewModel = EngineSimViewModel()

    var body: some View {
        NavigationView {
            Form {
                Section(header: Text("Connection")) {
                    HStack {
                        Text("Status")
                        Spacer()
                        Text(viewModel.connectionStatus)
                            .foregroundColor(viewModel.isConnected ? .green : .red)
                    }
                }

                Section(header: Text("Engine Telemetry")) {
                    GaugeView(
                        title: "RPM",
                        value: String(format: "%.0f", viewModel.rpm),
                        unit: "rpm",
                        color: .blue
                    )
                    GaugeView(
                        title: "Throttle",
                        value: String(format: "%.0f%%", viewModel.throttlePosition * 100),
                        unit: "",
                        color: .green
                    )
                    GaugeView(
                        title: "Load",
                        value: String(format: "%.1f%%", viewModel.load * 100),
                        unit: "",
                        color: .orange
                    )
                    GaugeView(
                        title: "Exhaust Flow",
                        value: String(format: "%.2f", viewModel.exhaustFlow),
                        unit: "m\u{00B3}/s",
                        color: .red
                    )
                    GaugeView(
                        title: "Manifold Pressure",
                        value: String(format: "%.0f", viewModel.manifoldPressure),
                        unit: "Pa",
                        color: .purple
                    )
                }

                Section(header: Text("Throttle Control")) {
                    Slider(
                        value: $viewModel.throttlePosition,
                        in: 0...1,
                        step: 0.01
                    ) {
                        Text("Throttle")
                    }
                    .disabled(!viewModel.isConnected)
                }

                Section(header: Text("Engine Controls")) {
                    Toggle("Ignition", isOn: $viewModel.ignitionEnabled)
                        .disabled(!viewModel.isConnected)

                    Button(action: {
                        viewModel.setStarter(!viewModel.starterEnabled)
                    }) {
                        Text(viewModel.starterEnabled ? "Starter: ON" : "Starter: OFF")
                            .frame(maxWidth: .infinity)
                            .padding()
                            .background(viewModel.starterEnabled ? Color.blue : Color.gray)
                            .foregroundColor(.white)
                            .cornerRadius(8)
                    }
                    .disabled(!viewModel.isConnected)
                }
            }
            .navigationTitle("Engine Sim")
            .toolbar {
                ToolbarItem(placement: .navigationBarTrailing) {
                    Button(action: {
                        if viewModel.isConnected {
                            viewModel.stop()
                        } else {
                            viewModel.start()
                        }
                    }) {
                        Text(viewModel.isConnected ? "Stop" : "Start")
                            .foregroundColor(viewModel.isConnected ? .red : .green)
                    }
                }
            }
        }
    }
}

struct ContentView_Previews: PreviewProvider {
    static var previews: some View {
        ContentView()
    }
}
