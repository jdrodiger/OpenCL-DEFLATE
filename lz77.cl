//B is the input data O is the output
//or the size of current chunk of data to be processed
//N1 is the size of block B1 etc.
#define MAX_WIN_SIZE = 32768 // this should be the number of threads per work group
__kernel void lz77(__global char *B1,__global char *B2,__global char *B3,__global char *O2,__global char *O3,__global int N1,__global int N2,__global int N3,__global short fblk) {
  //fblk values
  //1 first and final block b2
  //2 first block is b2 final block is b3
  //3 b3 is final block
  //4 b2 final block
  //5 b2 first b3 not final
  //6 other
  //this is the kernel which will perform the lz77 compression algaorithm
  //which involves a sliding window and represents data as length distance pairs
  
  int gid = get_global_id(0);//the global id
  int grid = get_group_id(0);//the work group
  int lid = get_local_id(0);//the id within the work group
 
  __local volatile int winSize = 0; //size of sliding window
  __local volatile int LAloc = 0;   //index of the beginning of the lookahead
  __local volatile int Oloc = 0; //the output location
  __local volatile int bestMatchDist = 0; //the best match found
  __local volatile int bestMatchLength = 0; //the best match found

  int matchLength = 0;
  int matchStart = lid;
  short matching = 1;
  char byteMark = 2;

  //if its fblk insert 1 then 01 else 0 then 01
  if(lid == 0 && grid == 0){
    if(fblk == 1 || fblk == 4){ //if the first block is last blokc
      O2[0] = 0x60;
    }else if(fblk == 2 || fblk == 3){// if the second block is last block
      O2[0] = 0x40;
      O3[0] = 0x60;
    }else{//if neither is last block
      O2[0] = 0x40;
      O3[0] = 0x40;
    }
  }

  /*******************************************************/
  /* details about indexing in the window	         */
  /* if the thread is not in the first block 	         */
  /* then winsize is its maximum 		         */
  /* otherwize winsize grows over time		         */
  /* the local id is the distance back and 	         */
  /* index when not first block is the blocks size - lid */
  /* index when first block is laloc - lid	         */
  /*******************************************************/

  while (((LAloc < N2)&&(grid == 0)) || ((LAloc < N3)&&(grid == 1))) {
    if(winSize > 3) {
      if(lid <= winSize && matching){
	//loop through from the index of cur thread to end of window
	//if at a spot is a match atomic_max the match length
	//also do something to indicate who atomic matched
	//maybe just check if you(the thread) are the best at the end

	//new idea just set the winsize to zero for first block
	//if its not the first block simply have negative values go into the preious block
	
	//TODO check if this actually works
	//it does not because there is nothing which will deal with when the thread starts
	//in the previous block and then moves into the next one
	//wait maybe it does??????
	if(grid == 0){
	  for(int i = LAloc - (lid+1); i < LAloc; i++){
	    if(i<0){
	      if(B1[N1+i] == B2[LAloc+matchLength] && matching){
		matchLength++;
		atomic_max(&bestMatch,matchLength);
	      }else{
		matching = 0;
	      }  
	    }else{
	      if(B2[i] == B2[LAloc+matchLength] && matching){
		matchLength++;
		atomic_max(&bestMatch,matchLength);
	      }else{
		matching = 0;
	      }
	    }
	  }
	}else if((fblk != 1)&&(fblk != 4)){
	  for(int i = LAloc - (lid+1); i < LAloc; i++){
	    if(i<0){
	      if(B2[N1+i] == B3[LAloc+matchLength] && matching){
		matchLength++;
		atomic_max(&bestMatch,matchLength);
	      }else{
		matching = 0;
	      }  
	    }else{
	      if(B3[i] == B3[LAloc+matchLength] && matching){
		matchLength++;
		atomic_max(&bestMatch,matchLength);
	      }else{
		matching = 0;
	      }
	    }
	  }
	}

	//if you are the one who made the best match
	if(matchLength == bestMatch){
	  //enter length distance pair or whatever
	  bestMatchDist = matchStart;
	  bestMatchLength = matchLength;
	}
      }
    }else {
      winSize++;
      bestMatchLength = 0;
    }
    
    if(lid = 0 && gid == 0) {
      //this thread adds to the output after match is found
      //both generating the code and inserting it
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
      
      
      unsigned int toinsert;//the bits to insert
      unsigned char inLen;//how many bits to insert
      
      if(bestMatchLength < 3){
	//insert just a literal
	if(B2[LAloc] < 144){
	  toinsert = B2[LAloc] + 0x30;
	  toinsert <<= 24; //shift so that first bit is on left
	  inLen = 8;
	}else{
	  toinsert = B2[LAloc] + 110010000b;
	  toinsert <<= 23; //shift so that the first bit is in the far left
	  inLen = 9;
	}
	//insert into the output array
	for(;byteMark<inLen;byteMark++){
	  O2[Oloc] >>= 1;//shift right one bit
	  O2[Oloc] = O2[LAloc] & 0x7f; //set bit to zero //this is maybe not needed but im being safe
	  if(toinsert & 0x80000000)
	    O2[Oloc] |= 0x80;
	  toinsert <<=1;
	  if(byteMark == 8){
	    byteMark == 0;
	    Oloc++;
	  }
	}
      }else{
	//insert length distance codes
	if(bestMatchLength < 11){ 
	  //257 - 264 zero extra bits 
	  inLen = 7;
	  toinsert = bestMatchLength-3;
	  toinsert <<= 25;//push it to the left
	}else if(bestMatchLength < 19){
	  //265 - 268 one extra bit
	  inLen = 8;
	  toinsert = 8 + ((bestMatchLength - 11)/2);
	  toinsert <<= 25;
	  if(bestMatchLength%2 == 0)//add extra bit to indicate which length
	    toinsert ^= 0x01000000;
	}else if(bestMatchLength < 35){
	  //269 - 272 two exra bits
	  inLen = 9;
	  toinsert = 12 + ((bestMatchLength - 19)/4);
	  toinsert <<= 2;
	  toinsert ^= (bestMatchLength-19)%4;
	  toinsert <<= 24;
	}else if(bestMatchLength < 67){
	  //273 - 276 three extra bits
	  inLen = 10;
	  toinsert = 16 + ((bestMatchLength - 35)/8);
	  toinsert <<= 3;
	  toinsert ^= (bestMatchLength-35)%8;
	  toinsert <<= 22;
	}else if(bestMatchLength < 130){
	  //277 - 280 four extra bits
	  inLen = 11;
	  toinsert = 20 + ((bestMatchLength-67)/16);
	  toinsert <<= 4;
	  toinsert ^= (bestMatchLength-67)%16;
	  toinsert <<=21;
	}else if(bestMatchLength < 258){
	  //281 - 284 five extra bits
	  if (bestMatchLength == 130){//this uses four extra bits but it is in the new length
	    inLen = 12;
	    toinsert = 0xb0f00000;
	  }else{
	    inLen = 13;
	    toinsert = 0xc0 + ((bestMatchLength-131)/32);
	    toinsert <<= 5;
	    toinsert ^= (bestMatchLength-131)%32;
	    toinsert <<= 19;
	  }
	}else{
	  //length 258 code 285 0 extra bits
	  inLen = 8;
	  toinsert = 0xc5000000;
	}
        //insert into the output array
	for(;byteMark<inLen;byteMark++){
	  O2[Oloc] >>= 1;//shift right one bit
	  O2[Oloc] = O2[LAloc] & 0x7f; //set bit to zero //this is maybe not needed but im being safe
	  if(toinsert & 0x80000000)
	    O2[Oloc] |= 0x80;
	  toinsert <<=1;
	  if(byteMark == 8){
	    byteMark == 0;
	    Oloc++;
	  }
	}
	//TODO Check each of these calculations to ensure proper function
	//distance five bits plus extra bits depending
	//all distances are five bits not from the other table
	//up to 13 extra bits 
	if(bestMatchDist < 5){
	  //0eb
	  inLen = 5;
	  toinsert = bestMatchDist;
	  toinsert <<= 27;
	}else if(bestMatchDist < 9){
	  //1eb
	  inLen = 6;
	  toinsert = 4 + (bestMatchDist-5)/2;
	  toinsert <<= 1;
	  toinsert ^= (bestMatchDist-5)%2;
	  toinsert <<= 26;
	}else if(bestMatchDist < 17){
	  //2eb
	  inLen = 7;
	  toinsert = 6 + (bestMatchDist-9)/4;
	  toinsert <<= 2;
	  toinsert ^= (bestMatchDist-9)%4;
	  toinsert <<= 25;
	}else if(bestMatchDist < 33){
	  //3eb
	  inLen = 8;
	  toinsert = 8 + (bestMatchDist-17)/8;
	  toinsert <<= 3;
	  toinsert ^= (bestMatchDist-17)%8;
	  toinsert <<= 24;
	}else if(bestMatchDist < 65){
	  //4eb
	  inLen = 9;
	  toinsert = 10 + (bestMatchDist-33)/16;
	  toinsert <<= 4;
	  toinsert ^= (bestMatchDist-33)%16;
	  toinsert <<= 23;
	}else if(bestMatchDist < 129){
	  //5eb
	  inLen = 10;
	  toinsert = 12 + (bestMatchDist-65)/32;
	  toinsert <<= 5;
	  toinsert ^= (bestMatchDist-65)%32;
	  toinsert <<= 22;
	}else if(bestMatchDist < 257){
	  //6eb
	  inLen = 11;
	  toinsert = 14 + (bestMatchDist-129)/64;
	  toinsert <<= 6;
	  toinsert ^= (bestMatchDist-129)%64;
	  toinsert <<= 21;
	}else if(bestMatchDist < 513){
	  //7eb
	  inLen = 12;
	  toinsert = 16 + (bestMatchDist-257)/128;
	  toinsert <<= 7;
	  toinsert ^= (bestMatchDist-257)%128;
	  toinsert <<= 20;
	}else if(bestMatchDist < 1025){
	  //8eb
	  inLen = 13;
	  toinsert = 18 + (bestMatchDist-513)/256;
	  toinsert <<= 8;
	  toinsert ^= (bestMatchDist-513)%256;
	  toinsert <<= 19;
	}else if(bestMatchDist < 2049){
	  //9eb
	  inLen = 14;
	  toinsert = 20 + (bestMatchDist-1025)/512;
	  toinsert <<= 9;
	  toinsert ^= (bestMatchDist-1025)%512;
	  toinsert <<= 18;
	}else if(bestMatchDist < 4097){
	  //10eb
	  inLen = 15;
	  toinsert = 22 + (bestMatchDist-2049)/1024;
	  toinsert <<= 10;
	  toinsert ^= (bestMatchDist-2049)%1024;
	  toinsert <<= 17;
	}else if(bestMatchDist < 8193){
	  //11eb
	  inLen = 16;
	  toinsert = 24 + (bestMatchDist-4097)/2048;
	  toinsert <<= 11;
	  toinsert ^= (bestMatchDist-4097)%2048;
	  toinsert <<= 16;
	}else if(bestMatchDist < 16385){
	  //12eb
	  inLen = 17;
	  toinsert = 26 + (bestMatchDist-8193)/4096;
	  toinsert <<= 12;
	  toinsert ^= (bestMatchDist-8193)%4096;
	  toinsert <<= 15;
	}else {
	  //13eb
	  inLen = 18;
	  toinsert = 28 + (bestMatchDist-16385)/8192;
	  toinsert <<= 13;
	  toinsert ^= (bestMatchDist-16385)%8192;
	  toinsert <<= 14;
	}

        //insert into the output array
	for(;byteMark<inLen;byteMark++){
	  O2[Oloc] >>= 1;//shift right one bit
	  O2[Oloc] = O2[LAloc] & 0x7f; //set bit to zero //this is maybe not needed but im being safe
	  if(toinsert & 0x80000000)
	    O2[Oloc] |= 0x80;
	  toinsert <<=1;
	  if(byteMark == 8){
	    byteMark == 0;
	    Oloc++;
	  }
	}
      }
      if(winSize < MAX_WIN_SIZE){
	atomic_add(&winSize,1);//increment win size if its less than the max
      }
      atomic_add(&LAloc,1);//increment the look ahead location by one
    }
    //this is if there are two blocks being processed at once
    if(lid = 0 && gid == 1 && !((fblk == 1) || (fblk == 4))) {
      //this thread adds to the output after match is found
      //both generating the code and inserting it
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
      
      
      unsigned int toinsert;//the bits to insert
      unsigned char inLen;//how many bits to insert
      
      if(bestMatchLength < 3){
	//insert just a literal
	if(B3[LAloc] < 144){
	  toinsert = B3[LAloc] + 0x30;
	  toinsert <<= 24; //shift so that first bit is on left
	  inLen = 8;
	}else{
	  toinsert = B3[LAloc] + 110010000b;
	  toinsert <<= 23; //shift so that the first bit is in the far left
	  inLen = 9;
	}
	//insert into the output array
	for(;byteMark<inLen;byteMark++){
	  O3[Oloc] >>= 1;//shift right one bit
	  O3[Oloc] = O3[LAloc] & 0x7f; //set bit to zero //this is maybe not needed but im being safe
	  if(toinsert & 0x80000000)
	    O3[Oloc] |= 0x80;
	  toinsert <<=1;
	  if(byteMark == 8){
	    byteMark == 0;
	    Oloc++;
	  }
	}
      }else{
	//insert length distance codes
	if(bestMatchLength < 11){ 
	  //257 - 264 zero extra bits 
	  inLen = 7;
	  toinsert = bestMatchLength-3;
	  toinsert <<= 25;//push it to the left
	}else if(bestMatchLength < 19){
	  //265 - 268 one extra bit
	  inLen = 8;
	  toinsert = 8 + ((bestMatchLength - 11)/2);
	  toinsert <<= 25;
	  if(bestMatchLength%2 == 0)//add extra bit to indicate which length
	    toinsert ^= 0x01000000;
	}else if(bestMatchLength < 35){
	  //269 - 272 two exra bits
	  inLen = 9;
	  toinsert = 12 + ((bestMatchLength - 19)/4);
	  toinsert <<= 2;
	  toinsert ^= (bestMatchLength-19)%4;
	  toinsert <<= 24;
	}else if(bestMatchLength < 67){
	  //273 - 276 three extra bits
	  inLen = 10;
	  toinsert = 16 + ((bestMatchLength - 35)/8);
	  toinsert <<= 3;
	  toinsert ^= (bestMatchLength-35)%8;
	  toinsert <<= 22;
	}else if(bestMatchLength < 130){
	  //277 - 280 four extra bits
	  inLen = 11;
	  toinsert = 20 + ((bestMatchLength-67)/16);
	  toinsert <<= 4;
	  toinsert ^= (bestMatchLength-67)%16;
	  toinsert <<=21;
	}else if(bestMatchLength < 258){
	  //281 - 284 five extra bits
	  if (bestMatchLength == 130){//this uses four extra bits but it is in the new length
	    inLen = 12;
	    toinsert = 0xb0f00000;
	  }else{
	    inLen = 13;
	    toinsert = 0xc0 + ((bestMatchLength-131)/32);
	    toinsert <<= 5;
	    toinsert ^= (bestMatchLength-131)%32;
	    toinsert <<= 19;
	  }
	}else{
	  //length 258 code 285 0 extra bits
	  inLen = 8;
	  toinsert = 0xc5000000;
	}
        //insert into the output array
	for(;byteMark<inLen;byteMark++){
	  O3[Oloc] >>= 1;//shift right one bit
	  O3[Oloc] = O3[LAloc] & 0x7f; //set bit to zero //this is maybe not needed but im being safe
	  if(toinsert & 0x80000000)
	    O3[Oloc] |= 0x80;
	  toinsert <<=1;
	  if(byteMark == 8){
	    byteMark == 0;
	    Oloc++;
	  }
	}
	//TODO Check each of these calculations to ensure proper function
	//distance five bits plus extra bits depending
	//all distances are five bits not from the other table
	//up to 13 extra bits 
	if(bestMatchDist < 5){
	  //0eb
	  inLen = 5;
	  toinsert = bestMatchDist;
	  toinsert <<= 27;
	}else if(bestMatchDist < 9){
	  //1eb
	  inLen = 6;
	  toinsert = 4 + (bestMatchDist-5)/2;
	  toinsert <<= 1;
	  toinsert ^= (bestMatchDist-5)%2;
	  toinsert <<= 26;
	}else if(bestMatchDist < 17){
	  //2eb
	  inLen = 7;
	  toinsert = 6 + (bestMatchDist-9)/4;
	  toinsert <<= 2;
	  toinsert ^= (bestMatchDist-9)%4;
	  toinsert <<= 25;
	}else if(bestMatchDist < 33){
	  //3eb
	  inLen = 8;
	  toinsert = 8 + (bestMatchDist-17)/8;
	  toinsert <<= 3;
	  toinsert ^= (bestMatchDist-17)%8;
	  toinsert <<= 24;
	}else if(bestMatchDist < 65){
	  //4eb
	  inLen = 9;
	  toinsert = 10 + (bestMatchDist-33)/16;
	  toinsert <<= 4;
	  toinsert ^= (bestMatchDist-33)%16;
	  toinsert <<= 23;
	}else if(bestMatchDist < 129){
	  //5eb
	  inLen = 10;
	  toinsert = 12 + (bestMatchDist-65)/32;
	  toinsert <<= 5;
	  toinsert ^= (bestMatchDist-65)%32;
	  toinsert <<= 22;
	}else if(bestMatchDist < 257){
	  //6eb
	  inLen = 11;
	  toinsert = 14 + (bestMatchDist-129)/64;
	  toinsert <<= 6;
	  toinsert ^= (bestMatchDist-129)%64;
	  toinsert <<= 21;
	}else if(bestMatchDist < 513){
	  //7eb
	  inLen = 12;
	  toinsert = 16 + (bestMatchDist-257)/128;
	  toinsert <<= 7;
	  toinsert ^= (bestMatchDist-257)%128;
	  toinsert <<= 20;
	}else if(bestMatchDist < 1025){
	  //8eb
	  inLen = 13;
	  toinsert = 18 + (bestMatchDist-513)/256;
	  toinsert <<= 8;
	  toinsert ^= (bestMatchDist-513)%256;
	  toinsert <<= 19;
	}else if(bestMatchDist < 2049){
	  //9eb
	  inLen = 14;
	  toinsert = 20 + (bestMatchDist-1025)/512;
	  toinsert <<= 9;
	  toinsert ^= (bestMatchDist-1025)%512;
	  toinsert <<= 18;
	}else if(bestMatchDist < 4097){
	  //10eb
	  inLen = 15;
	  toinsert = 22 + (bestMatchDist-2049)/1024;
	  toinsert <<= 10;
	  toinsert ^= (bestMatchDist-2049)%1024;
	  toinsert <<= 17;
	}else if(bestMatchDist < 8193){
	  //11eb
	  inLen = 16;
	  toinsert = 24 + (bestMatchDist-4097)/2048;
	  toinsert <<= 11;
	  toinsert ^= (bestMatchDist-4097)%2048;
	  toinsert <<= 16;
	}else if(bestMatchDist < 16385){
	  //12eb
	  inLen = 17;
	  toinsert = 26 + (bestMatchDist-8193)/4096;
	  toinsert <<= 12;
	  toinsert ^= (bestMatchDist-8193)%4096;
	  toinsert <<= 15;
	}else {
	  //13eb
	  inLen = 18;
	  toinsert = 28 + (bestMatchDist-16385)/8192;
	  toinsert <<= 13;
	  toinsert ^= (bestMatchDist-16385)%8192;
	  toinsert <<= 14;
	}

        //insert into the output array
	for(;byteMark<inLen;byteMark++){
	  O3[Oloc] >>= 1;//shift right one bit
	  O3[Oloc] = O3[LAloc] & 0x7f; //set bit to zero //this is maybe not needed but im being safe
	  if(toinsert & 0x80000000)
	    O3[Oloc] |= 0x80;
	  toinsert <<=1;
	  if(byteMark == 8){
	    byteMark == 0;
	    Oloc++;
	  }
	}
      }


      if(winSize < MAX_WIN_SIZE){
	atomic_add(&winSize,1);//increment win size if its less than the max
      }
      atomic_add(&LAloc,1);//increment the look ahead location by one
    }
  }
  unsigned int toinsert;//the bits to insert
  unsigned char inLen;//how many bits to insert
  if(lid = 0 && gid == 0) {
    toinsert = 0;
    inLen = 7;
    for(;byteMark<inLen;byteMark++){
      O2[Oloc] >>= 1;//shift right one bit
      O2[Oloc] = O2[LAloc] & 0x7f; //set bit to zero //this is maybe not needed but im being safe
      if(toinsert & 0x80000000)
	O2[Oloc] |= 0x80;
      toinsert <<=1;
      if(byteMark == 8){
	byteMark == 0;
	Oloc++;
      }
    }
    if(byteMark != 0)
      O2[Oloc] = 0;
  }
  if(lid = 0 && gid == 1 && !((fblk == 1) || (fblk == 4))) {
    toinsert = 0;
    inLen = 7;
    for(;byteMark<inLen;byteMark++){
      O3[Oloc] >>= 1;//shift right one bit
      O3[Oloc] = O3[LAloc] & 0x7f; //set bit to zero //this is maybe not needed but im being safe
      if(toinsert & 0x80000000)
	O3[Oloc] |= 0x80;
      toinsert <<=1;
      if(byteMark == 8){
	byteMark == 0;
	Oloc++;
      }
    }
    if(byteMark != 0)
      O2[Oloc] = 0;
  }
  //setlengths of output
  if(lid == 0 && gid == 0){
    N2 = Oloc;
  }
  if(lid = 0 && gid == 1 && !((fblk == 1) || (fblk == 4))) {
    N2 = Oloc;
  }
}
