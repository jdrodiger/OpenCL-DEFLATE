__kernel void add_one(__global char *A,__global char *C) {
  int i = get_global_id(0);

  C[i] = A[i];
}
