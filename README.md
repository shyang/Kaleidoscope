objc metadata generator - gathering signatures for all methods/functions/variable etc

### Prerequisites

* `brew install jsoncpp llvm`

### Usage:

```

$ Kaleidoscope -isysroot <SDK> -arch <ARCH> [-I <INCLUDE_PATH>] <HEADER>

ARCH:
  iPhoneOS.sdk: armv7 or arm64
  iPhoneSimulator.sdk i386 or x86_64
```

e.g.:

```
$ Kaleidoscope -isysroot /Applications/Xcode.app/Contents/Developer/Platforms/iPhoneSimulator.platform/Developer/SDKs/iPhoneSimulator.sdk -arch i386 /Applications/Xcode.app/Contents/Developer/Platforms/iPhoneSimulator.platform/Developer/SDKs/iPhoneSimulator.sdk/System/Library/Frameworks/Foundation.framework/Headers/NSArray.h
$ Kaleidoscope -isysroot /Applications/Xcode.app/Contents/Developer/Platforms/iPhoneSimulator.platform/Developer/SDKs/iPhoneSimulator.sdk -arch x86_64 /Applications/Xcode.app/Contents/Developer/Platforms/iPhoneSimulator.platform/Developer/SDKs/iPhoneSimulator.sdk/System/Library/Frameworks/Foundation.framework/Headers/NSArray.h
$ Kaleidoscope -isysroot /Applications/Xcode.app/Contents/Developer/Platforms/iPhoneOS.platform/Developer/SDKs/iPhoneOS.sdk -arch armv7 /Applications/Xcode.app/Contents/Developer/Platforms/iPhoneOS.platform/Developer/SDKs/iPhoneOS.sdk/System/Library/Frameworks/Foundation.framework/Headers/NSArray.h
$ Kaleidoscope -isysroot /Applications/Xcode.app/Contents/Developer/Platforms/iPhoneOS.platform/Developer/SDKs/iPhoneOS.sdk -arch arm64 /Applications/Xcode.app/Contents/Developer/Platforms/iPhoneOS.platform/Developer/SDKs/iPhoneOS.sdk/System/Library/Frameworks/Foundation.framework/Headers/NSArray.h
```

### Output
```json
{
	"class" : 
	{
		"NSArray" : 
		{
			"array" : "@8@0:4",
			"enumerateObjectsUsingBlock:" : "v12@0:4@?<v@?@I^c>8",
			...
		},
	},
	"enum" : 
	{
		"CFComparisonResult" : 
		{
			"kCFCompareEqualTo" : "0",
			"kCFCompareGreaterThan" : "1",
			"kCFCompareLessThan" : "-1"
		},
		...
	},
	"func" : 
	{
		"NSStringFromClass" : "@4#0",
		...
	},
	"protocols" : 
	[
		"NSObject",
		...
	],
	"var" : 
	{
		"NSFoundationVersionNumber" : "d",
		"kCFNull" : "r^{__CFNull=}"
		...
	}
}
```

输出内容有：

* 所有的类的方法 methods。特别的，block 参数不仅仅是 @?，而是完整签名 signature!
* 所有的全局函数 functions 和其签名 signature！
* 所有的全局变量 variables 和其签名 signature！
* 所有枚举 enum 的常量值

1. 用途1

	对于使用 block 对象作为参数的方法，反射返回的 signature 是 @?，如
	
	`-[NSArray enumerateObjectsUsingBlock:]` 的签名可得到 `v12@0:4@?8`
	
	如果事先用扫描头文件可得到：`v12@0:4@?<v@?@I^c>8` 其第三个参数 block 的签名应是 `v@?@I^c` 即 `void (blockSelf, NSUInteger, Pointer to Char/Bool)`，动态构建此参数时，可按此解析其输入输出。


2. 用途2

	全局方法 functions、variables 无法反射获取签名，必须扫描头文件。dlsym 获得地址后，可根据 signature 解析输入与输出。

3. 用途3

	enum 编译后不复存在，只能在头文件中获取其取值。




