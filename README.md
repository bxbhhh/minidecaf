## step8

阅读前请确保你已经知晓并理解了 lab12 readme.md 中的免责声明。

### 写在前面

这个 lab 有一定的难度，parse 和 codegen 工作量和难度都比较大，简单说明难点（不全）：
* 如何处理参数传递。你可以选择寄存器传参（这样的话你就可以调用库函数 print 了，很棒吧），也可以使用栈传参，总之你必须清楚将参数放到了什么位置，并正确计算 offset。同时注意参数顺序。
* 注意函数声明可能和定义参数不一致，所以即便已经有了声明，也一定要重新解析参数（主要是获取正确的参数名称）。

### 写在后面

放心，lab10 挺轻松的，正如暴风雨前的宁静。