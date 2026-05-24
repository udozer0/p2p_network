from conan import ConanFile
from conan.tools.cmake import cmake_layout


class P2PDemoConan(ConanFile):
    name = "p2p_demo"
    version = "0.1.0"
    package_type = "application"

    settings = "os", "compiler", "build_type", "arch"
    generators = "CMakeToolchain", "CMakeDeps"

    default_options = {
        "boost/*:header_only": True,
        "boost/*:error_code_header_only": True,
    }

    def requirements(self):
        self.requires("boost/1.91.0")

    def layout(self):
        cmake_layout(self)
