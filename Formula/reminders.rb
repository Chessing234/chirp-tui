# Homebrew formula (binary distribution). Fill sha256 after each release:
#   shasum -a 256 reminders-macos-universal
class Reminders < Formula
  desc "TUI reminder list with dual timed full-screen check-ins (CHIRP TUI)"
  homepage "https://github.com/Chessing234/chirp-tui"
  license "MIT"
  version "1.0.0"

  on_macos do
    url "https://github.com/Chessing234/chirp-tui/releases/download/v#{version}/reminders-macos-universal"
    sha256 "__SHA256_MAC_UNIVERSAL__"
  end

  on_linux do
    if Hardware::CPU.arm?
      url "https://github.com/Chessing234/chirp-tui/releases/download/v#{version}/reminders-linux-aarch64"
      sha256 "__SHA256_LINUX_AARCH64__"
    else
      url "https://github.com/Chessing234/chirp-tui/releases/download/v#{version}/reminders-linux-x86_64"
      sha256 "__SHA256_LINUX_X86_64__"
    end
  end

  def install
    bin.install "reminders-macos-universal" => "reminders" if OS.mac?
    bin.install "reminders-linux-aarch64" => "reminders" if OS.linux? && Hardware::CPU.arm?
    bin.install "reminders-linux-x86_64" => "reminders" if OS.linux? && Hardware::CPU.intel?
  end

  test do
    system "#{bin}/reminders", "--version"
  end
end
