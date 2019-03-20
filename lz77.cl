//B is the input data O is the output and N is either equal to the max lid
//or the size of current chunk of data to be processed
__kernel void lz77(__global char *B,__global char *O,__global int N) {
  //this is the kernel which will perform the lz77 compression algorithm
  //which involves a sliding window and represents data as length distance pairs
  
  int gid = get_global_id(0);//the global id
  int grid = get_group_id(0);//the work group
  int lid = get_local_id(0);//the id within the work group
 
  __global int winSize = 0;
  __global int LAloc = 0;
  __global int bestMatch = 0;

  int matchLength = 0;
  int matchStart;
  
  while (LAloc < N){
    if(winSize > 0) {
      if(gid < winSize){
      
      }
    }
    if(gid = 0) {
      //this thread add to the output after match is found
    }
  }
  //to search use atomic_max and atomic_xchg or something like that
  /*pseudo code for linear implementation
    while look-ahead buffer is not empty
      go backwards in search buffer to find longest match of look ahead buffer
      if match found
        print offset from boundary length of match next symbol in la
        shift window by length +1
      else
        print 0 0 first symbol in look ahead buffer
        shift window by one
      fi
    end while
   */
}
