## lpc 语言的简单实现

语法类似
```
inherit "3.txt";

string msg = "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";

static void swap(int *arr, int i, int j)
{
    mixed t = arr[i];
    arr[i] = arr[j];
    arr[j] = t;
}

static void quick_sort(int *arr, int l, int r)
{
    if (l >= r) return;

    int i = l, j = r;
    int p = arr[i];
    while (i < j) {
        while(i < j && arr[j] >= p) --j;
        while(i < j && arr[i] <= p) ++i;
        if (i < j) {
            swap(arr, i, j);
        }
    }

    arr[l] = arr[i];
    arr[i] = p;
    
    quick_sort(arr, l, i - 1);
    quick_sort(arr, i + 1, r);
}

int get_size()
{
    int *arr = [3, 1, 2, 0, 7, 8, 4, 5, 100, 78, 6, 0, 111, 222, 5];
    quick_sort(arr, 0, sizeof(arr) - 1);
    puts(arr);
    return sizeof(arr) ? sizeof(arr) : -1;
}

void test1()
{
    // id1 定义在 3.txt 里面
    id1 = 100;
    puts("====================");
    puts(id1);
    puts("====================");
}

void main()
{
    fun f = test1;
    mapping 好的 = {"hello": "world", "f": f};
    好的["aaa"]++;
    ++ ++ 好的["aaa"]++++;
    好的["f"]();
    puts(好的);

    for (int count = 0; count < 10000000 ; ++count) {
        mapping m1 = {};
    }
    
    int count = 100;
    
    get_size();
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


2023.5.30 此项目后续可能不再维护，因为好像不值得。
