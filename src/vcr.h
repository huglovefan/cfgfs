#pragma once

// https://www.fat-pie.com/vdt.htm

#if defined(WITH_VCR)

extern double vcr_get_timestamp(void);

extern void _vcr_begin(void);
extern void _vcr_end(void);

#define vcr_event\
	for (\
	int i = ({ _vcr_begin(); 1; });\
	i != 0;\
	i = ({ _vcr_end(); 0; })\
	)

extern void vcr_add_string(const char *, const char *);
extern void vcr_add_double(const char *, double);
extern void vcr_add_integer(const char *, long long);

extern void vcr_save(void);

#else

#define vcr_get_timestamp() 0

#define vcr_event\
	if(0)

#define vcr_add_string(_,v) (void)(v)
#define vcr_add_double(_,v) (void)(v)
#define vcr_add_integer(_,v) (void)(v)

#define vcr_save()

#endif
