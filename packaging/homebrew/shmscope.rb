class Shmscope < Formula
  desc "Live terminal viewer for POSIX shared memory"
  homepage "https://github.com/Ayush272002/shmscope"
  url "https://github.com/Ayush272002/shmscope/archive/refs/tags/v0.0.0.tar.gz"
  sha256 "0000000000000000000000000000000000000000000000000000000000000000"
  license "Apache-2.0"

  depends_on "cli11" => :build
  depends_on "cmake" => :build
  depends_on "nlohmann-json" => :build
  depends_on "ftxui"
  depends_on "highway"
  depends_on :macos
  depends_on "yaml-cpp"

  def install
    system "cmake", "-S", ".", "-B", "build",
                    "-DSHMSCOPE_USE_CONAN=OFF",
                    "-DSHMSCOPE_BUILD_TESTS=OFF",
                    "-DSHMSCOPE_BUILD_BENCHMARKS=OFF",
                    "-DSHMSCOPE_BUILD_EXAMPLES=OFF",
                    *std_cmake_args
    system "cmake", "--build", "build"
    system "cmake", "--install", "build"
    pkgshare.install "examples/ring.ksy", "examples/ring.yaml", "examples/ring.json"
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
