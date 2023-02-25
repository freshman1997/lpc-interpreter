#include <iostream>
#include <ctime>

#include "os.h"

using namespace std;

int main(int argc, char **argv)
{
	os::init_seed(time(NULL));
	cout << "random value: " << os::random() << endl;
	return 0;
}
