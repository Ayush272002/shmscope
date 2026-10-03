class Shmscope < Formula
  desc "Live terminal viewer for POSIX shared memory"
  homepage "https://github.com/Ayush272002/shmscope"
  url "https://github.com/Ayush272002/shmscope/releases/download/v0.0.0/shmscope-v0.0.0-macos-arm64.tar.gz"
  version "0.0.0"
  sha256 "0000000000000000000000000000000000000000000000000000000000000000"
  license "Apache-2.0"

  depends_on arch: :arm64
  depends_on :macos

  def install
    bin.install "bin/shmscope"
    pkgshare.install "examples"
  end

  test do
    assert_match version.to_s, shell_output("#{bin}/shmscope --version")

    (testpath/"broken.ksy").write <<~YAML
      meta: {id: broken, endian: le}
      seq: [{id: a, type: u3}]
    YAML
    output = shell_output("#{bin}/shmscope --layout #{testpath}/broken.ksy /nothing 2>&1", 1)
    assert_match "broken.ksy:2:", output
  end
end
