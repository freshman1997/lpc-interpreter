int strip_name(const char *src, char *dest, int size)
{
        char last_c = 0;
        char *p = dest;
        char *end = dest + size - 1;

        while (*src == '/') {
                src++;
        }

        while (*src && p < end) {
                if (last_c == '/' &&  *src == '/') {
                        return 0;
                }
                last_c = (*p++ =  *src++);
        }

        /* In some cases, (for example, object loading) this currently gets
         * run twice, once in find_object, and once in load object.  The
         * net effect of this is:
         * /foo.c -> /foo [no such exists, try to load] -> /foo created
         * /foo.c.c -> /foo.c [no such exists, try to load] -> /foo created
         *
         * causing a duplicate object crash.  There are two ways to fix this:
         * (1) strip multiple .c's so that the output of this routine is something
         *     that doesn't change if this is run again.
         * (2) make sure this routine is only called once on any name.
         *
         * The first solution is the one currently in use.
         */
        while (p - dest > 2 && p[ - 1] == 'c' && p[ - 2] == '.') {
                p -= 2;
        }

        *p = 0;
        return 1;
}
