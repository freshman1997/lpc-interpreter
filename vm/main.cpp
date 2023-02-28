#include <iostream>
#include <ctime>

#include "os/os.h"
#include "efun/efun.h"

extern void debug_message(const char *fmt, ...);

using namespace std;
int _abs(int num)
{
    if (num < 0) {
        /* 负数补码先减一, 再取反, 得到原码, 即正数 */
        return ~(--num);
    }
    return num;
}

int main(int argc, char **argv)
{
	debug_message("xxxxxxxxxxxxxxxxxxxxx %d:%d \n", 100, _abs(-1));
	os::init_seed(time(NULL));
	cout << "random value: " << os::random() << endl;
	return 0;
}
