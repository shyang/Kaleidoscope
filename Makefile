SignatureExtractor: main.cpp
	clang++ -fvisibility=hidden -std=c++11 -o SignatureExtractor main.cpp -I/usr/local/opt/jsoncpp/include -I/usr/local/opt/llvm/include -L/usr/local/opt/jsoncpp/lib /usr/local/opt/llvm/lib/lib{LLVM,clang}*.a -lz -lcurses -ljsoncpp_static
	otool -L SignatureExtractor # make sure no dynamic link against /usr/local

