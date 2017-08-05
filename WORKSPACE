workspace(name = "com_github_abergmeier_etc2comp")

git_repository(
    name = "io_bazel_rules_go",
    remote = "https://github.com/bazelbuild/rules_go.git",
    tag = "0.5.2",
)
load("@io_bazel_rules_go//go:def.bzl", "go_repositories")

go_repositories()

new_http_archive(
    name = "com_github_openimageio_oiio_images",
    url = "https://github.com/OpenImageIO/oiio-images/archive/9a70c65c7a29a50114a8208d61c87ba4fedd018e.zip",
    strip_prefix = "oiio-images-9a70c65c7a29a50114a8208d61c87ba4fedd018e",
    build_file_content = """
exports_files(["oiio-logo-with-alpha.png"])
""",
)

http_archive(
    name = "com_google_googletest",
    url = "https://github.com/abergmeier/googletest-bazel/archive/f07a67c6632a1e8c78eebbdcf95312ee190f0e66.zip",
    strip_prefix = "googletest-bazel-f07a67c6632a1e8c78eebbdcf95312ee190f0e66",
)
