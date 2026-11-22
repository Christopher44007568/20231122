# third_party — 本地 sysroot

本目录的 `sysroot/` 由 Ubuntu 26.04 官方 `.deb` 包解包生成（环境无 root
权限，无法系统级安装），提供构建所需的 Vulkan 头文件、GLFW 与
`glslangValidator`。运行时 Vulkan 使用系统 loader 与 Mesa lavapipe。

重新生成方式：

```sh
mkdir -p third_party/debs third_party/sysroot
cd third_party/debs
for p in libvulkan-dev libglfw3-dev libglfw3 glslang-tools; do
  u=$(apt-get download --print-uris "$p" | grep -o "http://[^']*" | head -1)
  curl -sO "$u"
done
cd ..
for d in debs/*.deb; do dpkg-deb -x "$d" sysroot; done
# libvulkan.so 是指向系统库的符号链接，需补齐实体：
cp /usr/lib/x86_64-linux-gnu/libvulkan.so.1.* sysroot/usr/lib/x86_64-linux-gnu/
ln -sf libvulkan.so.1.4.341 sysroot/usr/lib/x86_64-linux-gnu/libvulkan.so.1
```

`debs/` 与 `sysroot/` 均已加入 `.gitignore`。
