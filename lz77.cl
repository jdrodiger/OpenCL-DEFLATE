//B is the input data O is the output and N is either equal to the max lid
//or the size of current chunk of data to be processed if it is the first block to be processed fblk will be one if its the final fblk will be 2 if both 3 maybe 
__kernel void lz77(__global char *B1,__global char *B2,__global char *B3,__global char *O2,__global char *O3,__global int N2,__global int N3,__global short fblk) {
  //this is the kernel which will perform the lz77 compression algorithm
  //which involves a sliding window and represents data as length distance pairs
  
  int gid = get_global_id(0);//the global id
  int grid = get_group_id(0);//the work group
  int lid = get_local_id(0);//the id within the work group
 
  __local volatile int winSize = 0; //size of sliding window
  __local volatile int LAloc = 0;   //index of the beginning of the lookahead
  __local volatile int bestMatchStart = 0; //the best match found
  __local volatile int bestMatchLength = 0; //the best match found
  __local volatile int O2loc = 0;
  __local volatile int O3loc = 0;

  int matchLength = 0;
  int matchStart = lid;
  short matching = 1;
  char byteMark = 2;


  //todo add the code at the start of the block
  //if its fblk insert 1 then 01 else 0 then 01
  if(lid == 0){
    if(fblk == 1 && gid == ){
      
    }
    if(fblk == 2 || fblk == 3){
      if(grid = 0)
	O2[0] = 0x60;
      else if(grid == 1 && fblk == 3)
	O3[0] = 0x60;
    }else{
        if(grid = 0)
	  O2[0] = 0x40;
	else if(grid == 1)
	  O3[0] = 0x40;
    }
  }

  //starting out just coding the 2 array
  while (LAloc < N2) {
    if(winSize > 0) {
      if(gid <= winSize && matching){
	//loop through from the index of cur thread to end of window
	//if at a spot is a match atomic_max the match length
	//also do something to indicate who atomic matched
	//maybe just check if you(the thread) are the best at the end
	for(int i = lid;i < winSize && i + LAloc < N2;i++){
	  if(B[i] == B[LAloc+matchLength] && matching){
	    matchLength++;
	    atomic_max(&bestMatch,matchLength);
	  }
	  else{
	    matching = 0;
	  }
	}
	//if you are the one who made the best match
	
	if(matchLength == bestMatch){
	  //enter length distance pair or whatever
	  bestMatchStart = matchStart;
	  bestMatchLength = matchLength;
	}
	
      }
    }
    if(lid = 0) {
      //this thread add to the output after match is found
      /*
                   Lit Value    Bits        Codes
                   ---------    ----        -----
                     0 - 143     8          00110000 through
                                            10111111
                   144 - 255     9          110010000 through
                                            111111111
                   256 - 279     7          0000000 through
                                            0010111
                   280 - 287     8          11000000 through
                                            11000111
      */
      unsigned short toinsert;//the bits to insert
      unsigned char inLen;//how many bits to insert
      if(bestMatchLength < 3){
	//insert just a literal
	if(B2[LAloc] < 144){
	  toinsert = B2[LAloc] + 00110000b;
	  toinsert <<= 8; //shift so that first bit is on left
	  inLen = 8;
	}else{
	  toinsert = B2[LAloc] + 110010000b;
	  toinsert <<= 7; //shift so that the first bit is in the far left
	  inLen = 9;
	}
	for(;byteMark<inLen;byteMark++){
	  O2[O2loc] >>= 1;//shift right one bit
	  O2[O2loc] = O2[LAloc] & 0x7f; //set bit to zero
	  if(toinsert & 0x8000)
	    O2[O2loc] |= 0x80;
	  toinsert <<=1;
	}
      }else{
	//insert length distance codes
	if(bestMatchLength < 11){ 
	  //257 - 264 zero extra bits
	}else if(bestMatchLength < 19){
	  //265 - 268 one extra bit
	}else if(bestMatchLength < 35){
	  //269 - 272 two exra bits
	}else if(bestMatchLength < 67){
	  //273 - 276 three extra bits
	}else if(bestMatchLength < 131){
	  //277 - 280 four extra bits
	}else if(bestMatchLength < 258){
	  //281 - 284 five extra bits
	}else{
	  //length 258 code 285 0 extra bits
	}
	for(;byteMark<inLen;byteMark++){
	  O2[O2loc] >>= 1;//shift right one bit
	  O2[O2loc] = O2[LAloc] & 0x7f; //set bit to zero
	  if(toinsert & 0x8000)
	    O2[O2loc] |= 0x80;
	  toinsert <<=1;
	}
	//distance five bits plus extra bits depending
	
	
	for(;byteMark<inLen;byteMark++){
	  O2[O2loc] >>= 1;//shift right one bit
	  O2[O2loc] = O2[LAloc] & 0x7f; //set bit to zero
	  if(toinsert & 0x8000)
	    O2[O2loc] |= 0x80;
	  toinsert <<=1;
	}
      }

      //using 1024 temporarily, it is the max win size in this program
      if(winSize < 1024){
	atomic_add(&winSize,1);//increment win size if its less than the max
      }
      atomic_add(&LAloc,1);//increment the look ahead location by one
    }
  }
}
