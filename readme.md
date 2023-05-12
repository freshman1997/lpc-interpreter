## lpc 语言的简单实现

语法类似
```
static string* gHandlers = {
    "item": 100,
    "legion": 1000,
    "player": 10000
};

void hello()
{
    puts("你好，世界！！");
    puts(gHandlers);
    int count = 0;
    mapping p = {"c" : 0};
    while (1) {
        if (p["c"] > 1000) {
            sleep(3000);
            break;
        }
        p["c"] += 1;
        puts(p);
        mapping aa = {};
    }
}

void create()
{
    int *aa = [1, 2];
    ++aa[0];
    puts(aa);

    hello();
    test();
}

```
一个文件即为一个对象，有几个无参函数可以在创建、加载进来、释放的时候被依次调用，分别为：
```
    void create() {}
    void on_loadin() {}
    void on_destruct() {}
```
需要在指定的某一目录下运行，后续所有对象都以
``"char/user.c"``, ``"module/template/mail.c"`` 这种形式表示

测试请修改 ``vm/src/runtime/vm.cpp`` 下的加载二进制文件路径
