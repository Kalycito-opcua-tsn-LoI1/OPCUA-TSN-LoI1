#ifndef LIBC_TIME_H_
#define LIBC_TIME_H_

struct mytm {
    int tm_sec;
    int tm_min;
    int tm_hour;
    int tm_mday;
    int tm_mon;
    int tm_year;
    int tm_wday;
    int tm_yday;
    int tm_isdst;
    /* long __tm_gmtoff; */
    /* const char *__tm_zone; */
};

int __secs_to_tm(long long t, struct mytm *tm);

#endif /* LIBC_TIME_H_ */
