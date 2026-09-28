import os

from conan import ConanFile
from conan.tools.build import check_min_cppstd
from conan.tools.cmake import CMakeDeps, CMakeToolchain, cmake_layout
from conan.tools.files import load


class ShmscopeConan(ConanFile):
    name = "shmscope"
    description = "Live terminal viewer for POSIX shared memory."
    author = "Ayush Acharjya <ayushacharjya@gmail.com>"
    topics = ("cpp", "shared-memory", "tui", "debugging")

    settings = "os", "compiler", "build_type", "arch"

    def generate(self):
        CMakeDeps(self).generate()
        tc = CMakeToolchain(self)
        tc.user_presets_path = False
        tc.generate()

    def set_version(self):
        self.version = load(self, os.path.join(self.recipe_folder, "VERSION")).strip()

    def requirements(self):
        self.requires("ftxui/7.0.3")
        self.requires("yaml-cpp/0.9.0")
        self.requires("cli11/2.5.0")
        self.requires("highway/1.4.0")
        self.requires("nlohmann_json/3.12.0")

    def build_requirements(self):
        self.test_requires("gtest/1.17.0")
        self.test_requires("benchmark/1.9.1")

    def validate(self):
        check_min_cppstd(self, 23)

    def layout(self):
        cmake_layout(self)
