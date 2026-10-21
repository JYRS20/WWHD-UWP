extern long good(long,long); long bad(long a){__attribute__((musttail)) return good(a,1);}
