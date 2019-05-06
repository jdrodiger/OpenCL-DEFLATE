//B is the input data O is the output
//or the size of current chunk of data to be processed
//N1 is the size of block B1 etc.
#define MAX_WIN_SIZE 32768 // this should be the number of threads per work group

__kernel void lz77(__global unsigned char *B1,__global unsigned char *B2,__global unsigned char *O2,__global int * N1,__global int * N2,__global short * fblk) {
  //fblk values
  //1 first and final block b2
  //2 first block is b2 final block is b3
  //3 b3 is final block
  //4 b2 final block
  //5 b2 first b3 not final
  //6 other
  //this is the kernel which will perform the lz77 compression algaorithm
  //which involves a sliding window and represents data as length distance pairs
  //

  int gid = get_global_id(0);//the global id
  int grid = get_group_id(0);//the work group
  int lid = get_local_id(0);//the id within the work group
 
  __local volatile int winSize; //size of sliding window
  __local volatile int LAloc;   //index of the beginning of the lookahead
  __local volatile int Oloc; //the output location
  __local volatile int bestMatchDist; //the best match found
  __local volatile int bestMatchLength; //the best match found

  //initialize local variables
  if(lid == 0){
    winSize = 0;
    LAloc = 0;
    Oloc = 0;
    bestMatchDist = 0;
    bestMatchLength = 0;
  }

  int matchLength = 0;
  int matchStart = lid;
  short matching = 1;
  __local unsigned char byteMark;
  if(lid == 0) byteMark = 3;

  //if its *fblk insert 1 then 01 else 0 then 01
  if(lid == 0 && grid == 0){
    if(*fblk == 1 || *fblk == 4){ //if the first block is last blokc
      O2[0] = 0x60;
    }else if(*fblk == 2 || *fblk == 3){// if the second block is last block
      O2[0] = 0x40;
      //      O3[0] = 0x60;
    }else{//if neither is last block
      O2[0] = 0x40;
      //      O3[0] = 0x60;
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
  //  if(lid == 0 && grid == 1){
  //    printf("n3 %d\nfblk %x\n",*N3,*fblk);
  //  }

  int i = 0;
  int t = 0;
  while ((LAloc < *N2)&&(grid == 0)) {
    //if(lid == 0){
    //printf("GRID: %d LALOC: %d\n",grid,LAloc);
      //}
    // barrier(CLK_GLOBAL_MEM_FENCE);
    matchLength = 0;
    if(lid == 0){
      bestMatchLength = 0;
      bestMatchDist = 0;
    }
    if(winSize >= 3) {
      for(t = 0; t < 32; t++){
	barrier(CLK_GLOBAL_MEM_FENCE);
	i = 0;
	matchStart = lid + (1024*t) + 1;
	if(matchStart <= winSize && matching){
	  //so this needs to be done 32 times with 1024 threads
	  if(grid == 0){
	    for(i = LAloc - (matchStart); i < LAloc; i++){
	      //	      if(lid == 0)
	      //		printf("i %d\n",i);
	      if((i<0)){
		printf("hmm2 i = %d\n",i);
		if(B1[*N1+i] == B2[LAloc+matchLength] && matching){
		  matchLength++;
		  atomic_max(&bestMatchLength,matchLength);
		}else{
		  matching = 0;
		}  
	      }else{
		if(LAloc+matchLength >= *N2){
		  //printf("thishap\n");
		  matching = 0;
		  break;
		}

		//		printf("i %d  LAloc+matchlength %d\n",i,LAloc+matchLength);
		if(B2[i] == B2[LAloc+matchLength] && matching){
		  matchLength++;
		  atomic_max(&bestMatchLength,matchLength);
		}else{
		  matching = 0;
		}
	      }
	    }
	  }
	  //if(lid == 0 && grid == 0)
	  //printf("laloc %d,BML %d\n",LAloc,bestMatchLength);
	  //if you are the one who made the best match
	  if(matchLength == bestMatchLength){
	    //enter length distance pair or whatever
	    //bestMatchDist = matchStart;
	    atomic_xchg(&bestMatchDist,matchStart);
	    //printf("%d\n",bestMatchLength);
	    //bestMatchLength = matchLength;
	    matchLength = 0;
	  }
	}
	matchLength = 0;
	matching = 1;
      }
    }else {
      //winSize++;
      if(lid == 0){
	bestMatchLength = 1;
      }
    }
    if(lid == 0 && grid == 0) {
      char printthis = 0;
      //      if(LAloc <= 1675){
      //	printthis = 1;
      //	printf("inserting: LAloc %d  bestMatchLength  %d  bestMatchDist %d  oloc %d  char %c\n",LAloc,bestMatchLength,bestMatchDist,Oloc,B2[LAloc]);
      //      }
      //this thread adds to the output after match is found
      //both generating the code and inserting it
      unsigned int toinsert = 0;//the bits to insert
      unsigned char inLen = 0;//how many bits to insert
      unsigned short extrabits = 0; //the extra bits to be inserted in reverse
      unsigned char eblen = 0; //the number of extra bits
      if(bestMatchLength < 3){
	//insert just a literal
	//	printf("laloc %d\n",LAloc);
	if(B2[LAloc] < 144){
	  //printf("in: %c %x\n",B2[LAloc],B2[LAloc]);
	  toinsert = B2[LAloc] + 0x30;
	  toinsert <<= 24; //shift so that first bit is on left
	  inLen = 8;
	}else{
	  toinsert = B2[LAloc] + 0x190;
	  toinsert <<= 23; //shift so that the first bit is in the far left
	  inLen = 9;
	}

	
	//insert into the output array
	for(int j = 0;j<inLen;j++){
	  O2[Oloc] >>= 1;//shift right one bit
	  O2[Oloc] = O2[Oloc] & 0x7f; //set bit to zero //this is maybe not needed but im being safe
	  if(toinsert & 0x80000000){
	    O2[Oloc] |= 0x80;
	  }
	  toinsert <<=1;
	  byteMark++;
	  if(byteMark == 8){
	    byteMark = 0;
	    Oloc++;
	    //	    printf("oloc %d\n",Oloc);
	    O2[Oloc] = 0;
	  }
	}//0110 1001 10
      }else{
	//insert length distance codes
	if(bestMatchLength < 11){ 
	  //257 - 264 zero extra bits
	  inLen = 7;
	  toinsert = bestMatchLength-2;
	  toinsert <<= 25;//push it to the left
	}else if(bestMatchLength < 19){
	  //265 - 268 one extra bit
	  inLen = 8;
	  toinsert = 9 + ((bestMatchLength - 11)/2);
	  toinsert <<= 25;
	  if(bestMatchLength%2 == 0)//add extra bit to indicate which length
	    toinsert ^= 0x01000000;
	}else if(bestMatchLength < 35){
	  //269 - 272 two exra bits
	  inLen = 7;
	  eblen = 2;
	  toinsert = 13 + ((bestMatchLength - 19)/4);
	  toinsert <<= 2;
	  extrabits = (bestMatchLength-19)%4;
	  toinsert <<= 23;
	}else if(bestMatchLength < 67){
	  //273 - 276 three extra bits
	  inLen = 7;
	  eblen = 3;
	  toinsert = 17 + ((bestMatchLength - 35)/8);
	  toinsert <<= 3;
	  extrabits = (bestMatchLength-35)%8;
	  toinsert <<= 22;
	}else if(bestMatchLength < 130){
	  //277 - 280 four extra bits
	  inLen = 7;
	  eblen = 4;
	  toinsert = 21 + ((bestMatchLength-67)/16);
	  toinsert <<= 4;
	  extrabits = (bestMatchLength-67)%16;
	  toinsert <<=21;
	}else if(bestMatchLength < 258){
	  //281 - 284 five extra bits
	  if (bestMatchLength == 130){//this uses four extra bits but it is in the new length
	    inLen = 12;
	    toinsert = 0xb0f00000;
	  }else{
	    inLen = 8;
	    eblen = 5;
	    toinsert = 0xc0 + ((bestMatchLength-131)/32);
	    toinsert <<= 5;
	    extrabits = (bestMatchLength-131)%32;
	    toinsert <<= 19;
	  }
	}else{
	  //length 258 code 285 0 extra bits
	  inLen = 8;
	  toinsert = 0xc5000000;
	}
        //insert into the output array
	for(int j = 0;j<inLen;j++){
	  O2[Oloc] >>= 1;//shift right one bit
	  O2[Oloc] = O2[Oloc] & 0x7f; //set bit to zero //this is maybe not needed but im being safe
	  if(toinsert & 0x80000000)
	    O2[Oloc] |= 0x80;
	  toinsert <<=1;
	  byteMark++;
	  if(byteMark == 8){
	    byteMark = 0;
	    Oloc++;
	    //	    printf("oloc %d\n",Oloc);
	    O2[Oloc] = 0;
	  }
	}
	//insert extra bits
	for(int j = 0;j<eblen;j++){
	  O2[Oloc] >>= 1;//shift right one bit
	  O2[Oloc] = O2[Oloc] & 0x7f; //set bit to zero //this is maybe not needed but im being safe
	  if(extrabits & 1)
	    O2[Oloc] |= 0x80;
	  extrabits >>=1;
	  byteMark++;
	  if(byteMark == 8){
	    byteMark = 0;
	    Oloc++;
	    //	    printf("oloc %d\n",Oloc);
	    O2[Oloc] = 0;
	  }
	}
	extrabits = 0; //the extra bits to be inserted in reverse
	eblen = 0; //the number of extra bits
	//TODO Check each of these calculations to ensure proper function
	//distance five bits plus extra bits depending
	//all distances are five bits not from the other table
	//up to 13 extra bits
	
	if(bestMatchDist < 5){
	  //0eb
	  inLen = 5;
	  toinsert = bestMatchDist-1;
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
	  inLen = 5;
	  eblen = 2;
	  toinsert = 6 + (bestMatchDist-9)/4;
	  toinsert <<= 2;
	  extrabits = (bestMatchDist-9)%4;
	  toinsert <<= 25;
	}else if(bestMatchDist < 33){
	  //3eb
	  inLen = 5;
	  eblen = 3;
	  toinsert = 8 + (bestMatchDist-17)/8;
	  toinsert <<= 3;
	  extrabits = (bestMatchDist-17)%8;
	  toinsert <<= 24;
	}else if(bestMatchDist < 65){ //0100 0100
	  //4eb
	  inLen = 5;
	  eblen = 4;
	  toinsert = 10 + (bestMatchDist-33)/16;
	  toinsert <<= 4;
	  extrabits = (bestMatchDist-33)%16;
	  toinsert <<= 23;
	}else if(bestMatchDist < 129){
	  //5eb
	  inLen = 5;
	  eblen = 5;
	  toinsert = 12 + (bestMatchDist-65)/32;
	  toinsert <<= 5;
	  extrabits = (bestMatchDist-65)%32;
	  toinsert <<= 22;
	}else if(bestMatchDist < 257){
	  //6eb
	  inLen = 5;
	  eblen = 6;
	  toinsert = 14 + (bestMatchDist-129)/64;
	  toinsert <<= 6;
	  extrabits = (bestMatchDist-129)%64;
	  toinsert <<= 21;
	}else if(bestMatchDist < 513){
	  //7eb
	  inLen = 5;
	  eblen = 7;
	  toinsert = 16 + (bestMatchDist-257)/128;
	  toinsert <<= 7;
	  extrabits = (bestMatchDist-257)%128;
	  toinsert <<= 20;
	}else if(bestMatchDist < 1025){
	  //8eb
	  inLen = 5;
	  eblen = 8;
	  toinsert = 18 + (bestMatchDist-513)/256;
	  toinsert <<= 8;
	  extrabits = (bestMatchDist-513)%256;
	  toinsert <<= 19;
	}else if(bestMatchDist < 2049){
	  //9eb
	  inLen = 5;
	  eblen = 9;
	  toinsert = 20 + (bestMatchDist-1025)/512;
	  toinsert <<= 9;
	  extrabits = (bestMatchDist-1025)%512;
	  toinsert <<= 18;
	}else if(bestMatchDist < 4097){
	  //10eb
	  inLen = 5;
	  eblen = 10;
	  toinsert = 22 + (bestMatchDist-2049)/1024;
	  toinsert <<= 10;
	  extrabits = (bestMatchDist-2049)%1024;
	  toinsert <<= 17;
	}else if(bestMatchDist < 8193){
	  //11eb
	  inLen = 5;
	  eblen = 11;
	  toinsert = 24 + (bestMatchDist-4097)/2048;
	  toinsert <<= 11;
	  extrabits = (bestMatchDist-4097)%2048;
	  toinsert <<= 16;
	}else if(bestMatchDist < 16385){
	  //12eb
	  inLen = 5;
	  eblen = 12;
	  toinsert = 26 + (bestMatchDist-8193)/4096;
	  toinsert <<= 12;
	  extrabits = (bestMatchDist-8193)%4096;
	  toinsert <<= 15;
	}else {
	  //13eb
	  inLen = 5;
	  eblen = 13;
	  toinsert = 28 + (bestMatchDist-16385)/8192;
	  toinsert <<= 13;
	  extrabits = (bestMatchDist-16385)%8192;
	  toinsert <<= 14;
	}
	//check what each begginging code is and what the extra bits are and if they match
	//printf("code %d, length %d\n",(toinsert>>27),bestMatchDist);


        //insert into the output array
	for(int j = 0;j<inLen;j++){
	  O2[Oloc] >>= 1;//shift right one bit
	  O2[Oloc] = O2[Oloc] & 0x7f; //set bit to zero //this is maybe not needed but im being safe
	  if(toinsert & 0x80000000)
	    O2[Oloc] |= 0x80;
	  toinsert <<=1;
	  byteMark++;
	  if(byteMark == 8){
	    byteMark = 0;
	    Oloc++;
	    //	    printf("oloc %d\n",Oloc);
	    O2[Oloc] = 0;
	  }
	}
	//insert extra bits
	for(int j = 0;j<eblen;j++){
	  O2[Oloc] >>= 1;//shift right one bit
	  O2[Oloc] = O2[Oloc] & 0x7f; //set bit to zero //this is maybe not needed but im being safe
	  if(extrabits & 1)
	    O2[Oloc] |= 0x80;
	  extrabits >>=1;
	  byteMark++;
	  if(byteMark == 8){
	    byteMark = 0;
	    Oloc++;
	    //	    printf("oloc %d\n",Oloc);
	    O2[Oloc] = 0;
	  }
	}
      }
      
      if(winSize < MAX_WIN_SIZE){
	winSize++;
      }
      if(bestMatchLength >= 3)
	LAloc += bestMatchLength;
      else
	LAloc++;
    }
  }
    //    if(lid == 0 && grid == 1)
    //      printf("did i reach here\n");
    //this is if there are two blocks being processed at once
  //  printf("gothere\n");

  unsigned int toinsert;//the bits to insert
  unsigned char inLen;//how many bits to insert
  unsigned short extra = 0;
  if(lid == 0 && grid == 0) {
    toinsert = 0;
    inLen = 7;
    for(int j = 0;j<inLen;j++){
      O2[Oloc] >>= 1;//shift right one bit
      O2[Oloc] = O2[Oloc] & 0x7f; //set bit to zero //this is maybe not needed but im being safe
      if(toinsert & 0x80000000)
	O2[Oloc] |= 0x80;
      toinsert <<=1;
      byteMark++;
      if(byteMark == 8){
	byteMark = 0;
	//printf("oloc %d\n",Oloc);
	Oloc++;
      }
    }
    
    extra = byteMark;
    if(byteMark == 0){
      Oloc--;
    }else
      for(;byteMark<8;byteMark++)
	O2[Oloc] >>= 1;
    // printf("O2olocend: %x,%x,%x\n",O2[Oloc-2],O2[Oloc-1],O2[Oloc]);

  }
  //setlengths of output and the number of bits off each subsequent block should be shifted
  if(lid == 0 && grid == 0){
    //printf("oloc %d\n",Oloc);
    //printf("extra2 %x\n",extra);
    *N2 = Oloc+1;
    *fblk = extra;
  }
}
