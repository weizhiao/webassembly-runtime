# WebAssembly Runtime
一个有解释器和JIT编译器的WebAssembly运行时，支持WASI（WASI来源于wamr）注：目前JIT编译器仍存在bug，在运行某些wasm程序时会出错

## 目录结构  
`build-scripts`：保存构建`llvm`的`python`脚本和运行时本身的构建文件`runtime_lib.cmake`  
`product`：在linux上运行的运行时  
`runtime`：运行时本身。`runtime/interperter`——解释器，`runtime/jit`——jit编译器


## 如何配置JIT编译器
`build_llvm.py`脚本会下载并构建`llvm 16.0.3`版本，该版本是运行时中`JIT`使用的`llvm`版本。  
```shell
cd build-scripts  
python3 ./build_llvm.py
```   
之后修改`product/linux/config.cmake`文件，将`RUNTIME_BUILD_JIT`设为`1`
```
set (RUNTIME_BUILD_JIT 1)
```

## 构建方式   
```shell
mkdir -p build
cd build  
cmake ..  
make  
```

## 如何使用
```shell
$ ./runtime printf.wasm
Hello, world!
```

