# webassembly runtime
一个有解释器和JIT编译器的WebAssembly运行时，支持WASI（WASI来源于wamr）   
本仓库供交流学习使用   
注：目前JIT编译器仍存在bug，在运行某些wasm程序时会出错

目录结构  
build-scripts：保存构建llvm的python脚本和运行时本身的构建文件runtime_lib.cmake  
product：在linux上运行的运行时  
runtime：运行时本身


使用JIT前需要执行以下命令：  
1 cd build-scripts  
2 python3 ./build_llvm.py   
build_llvm.py脚本会下载并构建llvm 16.0.3版本，该版本是运行时中JIT使用的llvm版本  

构建方式：  
1 在根目录下创建build目录  
2 cd build  
3 cmake ..  
4 make  
