/*===========================================================================*/
/* Written by Jarek Tuszynski (2005).                                        */
/* Software was developed in my private time, but it is distributed under    */
/* "caBIO Software License" like the rest of the caTools package             */
/*===========================================================================*/

//=======================================================================
//  Encoding Algorithm adapted from code by Christoph Hohmann
//  found at http://members.aol.com/rf21exe/gif.htm.
//  Which was adapted from code by Michael A, Mayer
//  found at http://www.danbbs.dk/%7Edino/whirlgif/gifcode.html
//  Parts of Decoding Algorithm were adapted from code by David Koblas.
//  It had the following notice:
//  "Copyright 1990 - 1994, David Koblas. (koblas@netcom.com)
//  Permission to use, copy, modify, and distribute this software and its 
//  documentation for any purpose and without fee is hereby granted, provided 
//  that the above copyright notice appear in all copies and that both that 
//  copyright notice and this permission notice appear in supporting 
//  documentation. This software is provided "as is" without express or 
//  implied warranty."
//=======================================================================
extern "C" {

#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>   // va_list
#include <string.h>   // memset, memcpy
typedef unsigned char uchar;

//#define TEST_ALG
#ifdef  TEST_ALG
  #define Calloc(b, t)  (t*) calloc(b,sizeof(t))
  #define Free free
  #define print printf

  inline void error(char *format, ...) 
  {
    va_list ap;
    fprintf(stderr, "\nError: ");
    va_start(ap, format);
    vfprintf(stderr, format, ap);
    va_end(ap);
    fprintf(stderr,".\n");
    exit(1);
  }

#else
  #include <R.h>
  #include <Rinternals.h>
  #define print Rprintf
#endif



inline int BitGet (int  num, int bit) { return ((num &  (1<<bit)) !=0 ); } 

#ifndef MAX
#define MAX(a,b)  ((a) < (b) ? (b) : (a))
#endif
#ifndef MIN
#define MIN(a,b)  ((a) > (b) ? (b) : (a))
#endif






//==============================================================
// bit-packer class
//==============================================================

int GetDataBlock(FILE *fp, uchar *buffer)
{
  int BlockSize = fgetc(fp);
  if (BlockSize==EOF) return -1;
  if (BlockSize== 0 ) return  0;
  if (!fread(buffer, BlockSize, 1, fp)) return -1;
  return BlockSize;
}


//=======================================================================
// Packs & unpacks a sequence of variable length codes into a buffer. Every 
// time 255 bytes have been completed, they are written to a binary file as
// a data block of 256 bytes (where the first byte is the 'bytecount' of the
// rest and therefore equals 255). Any remaining bits are moved to the
// buffer start to become part of the following block. After submitting
// the last code via submit(), the user must call WriteFlush() to write
// a terminal, possibly shorter, data block.
//=======================================================================
class BitPacker {
public:

  BitPacker()
  { // Constructor
    binfile   = NULL;
    need      = 8;
    pos       = buffer;
    *pos      = 0;
    bytesdone = 0;
    BlockSize = 255;
    curbit    = (2+BlockSize)<<3;
  }

  int  BytesDone() { return bytesdone; }
  void GetFile(FILE *bf) { binfile = bf; }

  //------------------------------------------------------------------------- 

  void SubmitCode(short code, short nBits)
  //  Packs an incoming 'code' of 'nBits' bits [1,12] to the buffer. As soon 
  //  as 255 bytes are full, they are written to 'binfile' as a data block 
  // end cleared from 'buffer'.
  { 
    // 'pos' points to a partially empty byte and
    // 'need' [1..8] tells how many bits will still fit in there. 
    // Since the bytes are filled bottom up (least significant bits first),
    // the 'need' vacant bits are the most significant ones.
    if (nBits<0 || nBits>12) error("BitPacker::SubmitCode");
    short mask;
    while (nBits >= need) {
      mask = (1<<need)-1; // 'mask'= all zeroes followed by 'need' ones
      *pos += (uchar)((mask&code) << (8-need)); // the 'need' lowest bits of 'code' fill the current byte at its upper end
      nBits -= need;  // update the length of 'code'
      code >>= need;  // remove the written bits from code
      *(++pos)=0;     // byte is now full, goto next byte & init it
      need=8;         // current byte can take 8 bits
    }                 // Now we have nBits < need.
    // (remainder of) code is written to the nBits rightmost free bits of 
    // the current byte. The current byte can still take 'need' bits, and 
    // we have 'need'>0. The bits will be filled upon future calls.
    if(nBits>0) {
      mask  = (1<<nBits)-1;        // 'mask'= all zeroes followed by 'need' ones
      *pos += (uchar)((mask&code)<<(8-need));
      need -= nBits;                         
    }    
    // As soon as 255 bytes are full, they are written to 'binfile' as a 
    // data block and removed from 'buffer'.
    if(pos-buffer >= 255) {         // pos pointing to buffer[255] or beyond
      fputc(255,binfile);           // write the "bytecount-byte"
      fwrite(buffer,255,1,binfile); // write buffer[0..254] to file
      buffer[0] = buffer[255];      // rotate the following bytes, which may still 
      buffer[1] = buffer[256];      // contain data, to the beginning of buffer, 
      pos -= 255;                   // point pos to the position for new input
      bytesdone += 256;
    }
  } // BitPacker::SubmitCode

  //------------------------------------------------------------------------- 

  void WriteFlush()
  //  Writes any data contained in 'buffer' to the file as one data block of
  //  1<= length<=255. 
  {
    // if the current byte is partially filled, leave it alone 
    if(need<8) pos++;  // close any partially filled terminal byte
    int BlockSize = (int) (pos-buffer);   // # remaining bytes
    if(BlockSize>0) { // buffer is empty
      fputc(BlockSize, binfile);
      fwrite(buffer, BlockSize, 1, binfile);
      bytesdone += BlockSize+1;
    }
  } // BitPacker::WriteFlush

  //------------------------------------------------------------------------- 

  short GetCode(short nBits)
  // Extract nBits [1:32] integer from the buffer. 
  // Read next data block if needed.
  {
    short i, j, code, lastbit;
    // if more bits is needed than we have stored in the buffer
    lastbit = (2+BlockSize)<<3;     // (BlockSize<<3 == BlockSize*8) - byte to bit conversion
    while ( (curbit+nBits) >= lastbit ) { // If () should have been enough but used while() just in case
      buffer[0] = buffer[BlockSize  ];
      buffer[1] = buffer[BlockSize+1];
      curbit   -= BlockSize<<3; 
      BlockSize = GetDataBlock(binfile, &buffer[2]);
      lastbit   = (2+BlockSize)<<3; // (BlockSize<<3 == BlockSize*8) - byte to bit conversion
      bytesdone += BlockSize+1;     // keep track of number of bytes read
    }
    // read next set of nBits from the buffer and store it into ret
    code = 0;
    i = curbit;
    for (j=0; j<nBits; j++, i++) 
      code |= BitGet(buffer[i>>3] , i&7) << j;
    curbit += nBits; // we read nBits of data from buffer 
    return code;
  }

  //------------------------------------------------------------------------- 
  
  int ReadFlush()
  { 
    short count;
    while ((count = GetDataBlock(binfile, buffer)) > 0);
    return count;
  }

private:
  FILE  *binfile;
  uchar  buffer[260];  // holds the total buffer of 256 + some extra
  uchar *pos;          // sliding pointer into buffer
  uchar  need;         // [1..8] tells how many bits will still fit in current byte
  int bytesdone;       // total number of bytes processed during the object's lifetime
  int curbit, BlockSize;
}; // class bitpacker


//==============================================================
// Gif-compression de compression functions
//==============================================================

//===========================================================================
// Contains the string-table, generates compression codes and writes them to a
// binary file, formatted in data blocks of maximum length 255 with
// additional bytecount header.
// Encodes the pixel data and writes the "raster data"-section of the GIF
// file, consisting of the "code size" byte followed by the counter-headed
// data blocks, including the terminating zero block.
// bf         must be an opened binary file to which the preceding parts
//            of the GIF format have been written
// data       is an array of bytes containing one pixel each and sorted
//            left to right, top to bottom. The first pixel is in data[0]
// nPixel     Number of pixels in the image
// nBit       Number of bits per pixel, where 2^nBit is the size of the GIF's
//            color tables. Allowed are 1..8. Used to determine 'nbits' and
//            the number of root codes. Max(data) HAS to be < 2^nBits
// returns:   The total number of bytes that have been written.
//------------------------------------------------------------------------- 
int EncodeLZW(FILE *bf, uchar *data, int nPixel, short nBits)
{
  BitPacker bp;          // object that does the packing and writing of the compression codes
  int    iPixel;         // pixel counter
  uchar  pixel;          // next pixel value to be encoded
  short  axon[4096], next[4096];  // arrays making up the string-table
  uchar  pix[4096];      // dito
  short  freecode;       // next code to be added to the string-table
  short  i, depth, cc, eoi, up, down, outlet;

  if (nPixel<0) error("EncodeLZW: nPixel can not be negative");
  if (nBits<1 || nBits>8) error(" EncodeLZW: nBit has to be between 1 and 8");

  // The following parameters will remain unchanged through the run
  depth  = MAX(2,nBits); // number of bits per data item (=pixel). Remains unchanged.
  cc     = 1<<depth;     // 'cc' or 'clear-code' Signals the clearing of the string-table.
  eoi    = cc+1;         // 'end-of-information'-code must be the last item of the code stream
  // initialize pixel reader
  nBits  = depth+1;      // current length of compression codes in bits [2, 12] (changes during encoding process)
  iPixel = 0;            // pixel #1 is next to be processed (iPixel will be pixel counter)          
  pixel  = data[iPixel]; // get pixel #1 
  // alocate and initialize memory
  bp.GetFile(bf);  // object packs the code and renders it to the binary file 'bf'
  for(i=0; i<cc; i++) pix[i] = (uchar)i; // Initialize the string-table's root nodes  

  // Write what the GIF specification calls the "code size". Allowed are [2..8].
  // This is the number of bits required to represent the pixel values. 
  fputc(depth,bf);             // provide data-depth to the decoder
  freecode = 4096;             // this will cause string-table flush first time around
  while(iPixel<nPixel) {       // continue untill all the pixels are processed
    if(freecode==(1<<nBits))   // if the latest code added to the string-table exceeds 'nbits' bits:
      nBits++;                 // increase size of compression codes by 1 bit     
    freecode++;                // freecode is only changed in this loop
    if(freecode>=4096) {       // free code is 2^12 the largest allowed
      // Flush the string-table  by removing the outlets of all root nodes. Everything is set to initial state.
      memset(axon, 0, 4096*sizeof(short)); // avoid string-table overflow
      bp.SubmitCode(cc,nBits); // tell the decoding software to flush its string-table                 
      nBits    = depth+1;      // reset nBits   
      freecode = cc+2;         // reset freecode
    }
    //  Writes the next code to the codestream and adds one entry to the string-table. 
    outlet=pixel;                 // start with the root node for 'pixel'
    // Follow the string-table and the data stream to the end of the
    // longest string that has a code
    do {
      up = outlet;
      iPixel++;                   // advance pixel counter (the only place it is advanced)
      if(iPixel >= nPixel) break; // end of data stream ? Terminate
      pixel = data[iPixel];       // get the value of the next pixel
      // Checks if the chain starting from headnode's axon (axon[up]) contains a node for 
      // 'pixel'. Returns that node's address (=outlet), or 0 if there is no such node.
      // 0 cannot be the root node 0, since root nodes occur in no chain.
      outlet = axon[up];
      while(outlet && pix[outlet]!=pixel) outlet=next[outlet];
    } while(outlet);

    // Submit 'up' which is the code of the longest string 
    bp.SubmitCode(up,nBits);
    if(iPixel >= nPixel) break;  // end of data stream ? Terminate

    // Extend the string by appending 'pixel':
    // Create a successor node for 'pixel' whose code is 'freecode'
    pix [freecode]=pixel;
    axon[freecode]=next[freecode]=0;
    // Link it to the end of the chain emanating from axon[up].
    // Don't link it to the start: it would slow down performance.
    down=axon[up];
    if(!down) axon[up]=freecode;
    else {
      while(next[down]) down=next[down];
      next[down]=freecode;
    }
  } // while()

  // Wrap up the file 
  bp.SubmitCode(eoi,nBits); // submit 'eoi' as the last item of the code stream
  bp.WriteFlush();   // write remaining codes including this 'eoi' to the binary file
  fputc(0,bf);       // write an empty data block to signal the end of "raster data" section in the file
  return 2 + bp.BytesDone();
} // EncodeLZW


//------------------------------------------------------------------------- 
// Reads the "raster data"-section of the GIF file and decodes the pixel 
// data.  Most work is done by GifDecomposer class and this function mostly 
// handles interlace row irdering
// bf         must be an opened binary file to which the preceding parts
//            of the GIF format have been written
// data       is an array of bytes containing one pixel each and sorted
//            left to right, top to bottom. 
// nPixels    Number of pixels in the image
// returns:   The total number of bytes that have been written.
//------------------------------------------------------------------------- 
int DecodeLZW(FILE *fp, uchar *data, int nPixel)
{
  BitPacker bp;                 // object that does the packing and writing of the
  short cc, eoi, freecode, nBits, depth, nStack, code, incode, firstcode, oldcode;
  short pix[4096], next[4096];
  uchar stack[4096];
  int iPixel, ret;

  freecode=nBits=firstcode=oldcode=0; // unnecesary line used to prevent warnings in gcc
  depth = fgetc(fp);             // number of bits per data item (=pixel). Remains unchanged.
  if (depth==EOF) return -1;
  bp.GetFile(fp);                // object packs the code and renders it to the binary file 'bf'
  cc    = 1<<depth;              // 'cc' or 'clear-code' Signals the clearing of the string-table.
  eoi   = cc+1;                  // 'end-of-information'-code must be the last item of the code stream

  code = cc;                     // this will force the code to flush & initialize the string-table first
  for(iPixel=0; iPixel<nPixel; ) { // read until all pixels were read
    if (iPixel) code=bp.GetCode(nBits);
    if (code == -1)  return 0;   // error
    if (code == eoi) break;      // 'end-of-information' - last item of the code stream 
    if (code == cc) {            // Flush the string-table. Everything is (re)set to initial state.
      memset(next, 0, 4096*sizeof(short));
      memset(pix , 0, 4096*sizeof(short));
      for (int i=0; i<cc; i++) pix[i] = i;
      nBits    = depth+1;
      freecode = cc+2;
      do { firstcode = bp.GetCode(nBits); } while (firstcode==cc); // keep on flushing until a non cc entry
      oldcode = firstcode;
      data[iPixel++] = (uchar) firstcode;
    } else {                     // the regular case
      nStack = 0;                // (re)initialize the stack    
      incode = code;             // store a copy of the code - it will be needed 
      if (code >= freecode) {
        stack[nStack++] = (uchar) firstcode;
        code            = oldcode;
      }
      while (code >= cc) {       // read string for code from string-table
        stack[nStack++] = (uchar) pix [code];
        code            = next[code];
      }
      firstcode      = pix[code];
      data[iPixel++] = (uchar) pix[code];
      while (nStack && iPixel<nPixel) 
        data[iPixel++] = stack[--nStack]; // if there is data on the stack return it
      if (freecode<4096) {       // free code is smaller than 2^12 the largest allowed
        next[freecode] = oldcode;// add to string-table
        pix [freecode] = firstcode;
        freecode++;
        if(freecode==(1<<nBits)) // if the latest code added to the string-table exceeds 'nbits' bits:
          nBits++;               // increase size of compression codes by 1 bit     
      }
      oldcode = incode;
    }
  } 
  if (bp.ReadFlush()==0) ret = 1+bp.BytesDone(); // 'end-of-information' - last item of the code stream 
  else ret = 0;
  return ret;  
}


//==============================================================
// Gif Writer
//==============================================================

void InterlaceShuffle(uchar* im, int width, int height, bool reverse=false)
{ // recreate Gif interlacing row order
  int i, row=0;
  uchar* from = Calloc(width*height, uchar);
  memcpy(from, im, width*height);
  if (reverse) {
    for (i=0; i<height; i+=8) memcpy(im+width*i, from+width*(row++), width);
    for (i=4; i<height; i+=8) memcpy(im+width*i, from+width*(row++), width);
    for (i=2; i<height; i+=4) memcpy(im+width*i, from+width*(row++), width);
    for (i=1; i<height; i+=2) memcpy(im+width*i, from+width*(row++), width);
  } else {
    for (i=0; i<height; i+=8) memcpy(im+width*(row++), from+width*i, width);
    for (i=4; i<height; i+=8) memcpy(im+width*(row++), from+width*i, width);
    for (i=2; i<height; i+=4) memcpy(im+width*(row++), from+width*i, width);
    for (i=1; i<height; i+=2) memcpy(im+width*(row++), from+width*i, width);
  }
  Free(from);
}

//------------------------------------------
inline int getint(uchar *buffer) { return (buffer[1]<<8) | buffer[0]; }
void fputw(int w, FILE *fp)
{ //  Write out a word to the GIF file
  fputc(  w     & 0xff, fp );
  fputc( (w>>8) & 0xff, fp );
}

//------------------------------------------


int imwriteGif(char* filename, uchar* data, int nRow, int nCol, int nBand, int nColor, 
               int *ColorMap,  bool interlace, int transparent, int DalayTime, char* comment)
{
  int B, i, rgb, imMax, filesize=0, Bands, band, n, m;
  int BitsPerPixel=0, ColorMapSize, Width, Height, nPixel;
  char fname[256], sig[16], *q;
  uchar *p=data;

  strcpy(fname,filename);
  i = (int) strlen(fname);
  if (fname[i-4]=='.') strcpy(strrchr(fname,'.'),".gif");
  Width  = nCol;
  Height = nRow;
  Bands  = nBand;
  nPixel = Width*Height;
  imMax  = data[0];
  n = nPixel*nBand;
  for(i=0; i<n; i++, p++) if(imMax<*p) imMax=*p;
  nColor=MIN(256,nColor);     // is a power of two between 2 and 256 compute its exponent BitsPerPixel (between 1 and 8)
  if (!nColor) nColor = imMax+1;
  if (imMax>nColor)
    error("ImWriteGif: Higher pixel values than size of color table");
  for(i=1; i<nColor; i*=2) BitsPerPixel++;  
  if (BitsPerPixel==0) BitsPerPixel=1;

  FILE *fp = fopen(fname,"wb");
  if (fp==0) return -1;

  //====================================
  // GIF Signature and Screen Descriptor
  //====================================
  if (transparent>=0 || comment || Bands>1) strcpy(sig,"GIF89a"); else strcpy(sig,"GIF87a");
  fwrite( sig, 1, 6, fp );           // Write the Magic header
  fputw( Width , fp );               // Bit 1&2 : Logical Screen Width 
  fputw( Height, fp );               // Bit 3&4 : Logical Screen Height 
  B = 0xf0 | (0x7&(BitsPerPixel-1)); // write BitsPerPixel-1 to the three least significant bits of byte 5 
  fputc( B, fp );                    // Bit 5: global color table (yes), color resolution, sort flag (no) size of global color table
  fputc( 0, fp );                    // Bit 6: Write out the Background color index
  fputc( 0, fp );                    // Bit 7: Byte of 0's (no aspect ratio info)

  //====================================
  // Global Color Map
  //====================================
  ColorMapSize = 1 << BitsPerPixel;
  if (ColorMap) {
    for( i=0; i<nColor; i++ ) {      // Write out the Global Colour Map
      rgb = ColorMap[i];
      fputc( (rgb >> 16) & 0xff, fp );
      fputc( (rgb >>  8) & 0xff, fp );
      fputc(  rgb        & 0xff, fp );
    }
  } else { // default gray-scale ramp
    for( i=0; i<nColor; i++ ) {        // Write out the Global Colour Map
      rgb = ((i*256)/nColor)  & 0xff;
      fputc( rgb, fp );
      fputc( rgb, fp );
      fputc( rgb, fp );
    }
  }
  for( i=nColor; i<ColorMapSize; i++ ) {  fputc(0,fp); fputc(0,fp); fputc(0,fp);  }

  //====================================
  // Extentions (optional)
  //====================================
  n = (comment ? (int)strlen(comment) : 0);
  if (n>0) {
	  fputc( 0x21, fp ); // GIF Extention Block introducer
	  fputc( 0xfe, fp ); // "Comment Extension" 
    for (q=comment; n>0; n-=255) {
      m = n<255 ? n : 255;
      fputc(m, fp );   
      fwrite(q, 1, m, fp );
      q += m;
      filesize += m+1;
    }
	  fputc( 0, fp );    // extention Block Terminator
    filesize += 3;
	}
  if (Bands>1) {
	  fputc( 0x21, fp ); // GIF Extention Block introducer
	  fputc( 0xff, fp ); // byte 2: 255 (hex 0xFF) Application Extension Label
    fputc( 11, fp );   // byte 3: 11 (hex (0x0B) Length of Application Block 
    fwrite("NETSCAPE2.0", 1, 11, fp ); // bytes 4 to 14: 11 bis of first sub-block
    fputc( 3, fp );    // byte 15: 3 (hex 0x03) Length of Data Sub-Block (three bytes of data to follow)
    fputc( 1, fp );    // byte 16: 1-means next number 2 bytes have iteration counter; 2-means next 4 bytes haveamount of memory needed
    fputw( 0, fp );    // byte 17&18: 0 to 65535, an unsigned integer. # of iterations the loop should be executed.
	  fputc( 0, fp );    // extention Block Terminator
    filesize += 19;
	}


  filesize += 6 + 7 + 3*ColorMapSize;
  for (band=0; band<Bands; band++) {
    if ( transparent >= 0 || Bands>1 ) {
      fputc( 0x21, fp );                // GIF Extention Block introducer "!"
      fputc( 0xf9, fp );                // "Graphic Control Extension" 
      fputc( 4, fp );                   // block is of size 4
      B  = (Bands>1 ? 2 : 0) << 2;      // Disposal Method
      B |= (0) << 1;                    // User Input flag: is user input needed?
      B |= (transparent >= 0 ? 1 : 0);  // Transparency flag
      fputc( B, fp );                   // "transparency color follows" flag
      fputw( DalayTime, fp );           // delay time in # of hundredths (1/100) of a second delay between frames
      fputc( (uchar) transparent, fp );
      fputc( 0, fp );                   // extention Block Terminator
      filesize += 8;
    }
  
    //====================================
    // Image Descriptor
    //====================================
    fputc( 0x2c  , fp );                // Byte 1  : Write an Image Separator ","
    fputw( 0     , fp );                // Byte 2&3: Write the Image left offset
    fputw( 0     , fp );                // Byte 4&5: Write the Image top offset
    fputw( Width , fp );                // Byte 6&7: Write the Image width
    fputw( Height, fp );                // Byte 8&9: Write the Image height
    fputc( interlace ? 0x40 : 0x00,fp); // Byte 10 : contains the interlaced flag 
    filesize += 10;

    //====================================
    // Raster Data (LZW encrypted)
    //====================================
    p = data+band*nPixel;
    if(interlace) InterlaceShuffle(p, Width, Height); // rearrange rows to do interlace 
    filesize += EncodeLZW(fp, p, nPixel, BitsPerPixel);
  }

  fputc(0x3b, fp );                     // Write the GIF file terminator ";"
  fclose(fp);
  return filesize+1;
}

//==============================================================
// Gif Reader
// Limitations:
// - single colormap (global or local) 
// - all bands will have same dimentions
//==============================================================

int ReadColorMap(FILE *fp, uchar byte, int ColorMap[256], int skip=0) 
{
  int i, nColor, ok=1;
  uchar buf[16];
  nColor = 2<<(byte&0x07);
  if ((byte&0x80)==0x80) {  
    if (skip) {
      char buffer[3*255];
      if (!fread(buffer, 3*nColor, 1, fp)) return 0;
    } else {
      for (i=0; i<nColor; i++) {
        if (!fread(buf, 3, 1, fp)) return 0;
        ColorMap[i] = (buf[0]<<16) | (buf[1]<<8) | buf[2];
      }
      while(i<256) ColorMap[i++] = -1;
    }
    ok = 2;
  }
  return ok;
}

//------------------------------------------

uchar* append(uchar *trg, uchar *src, int nPixel, int nBand )
{
  uchar* data = Calloc(nPixel*(nBand+1), uchar);
  int n = nPixel*nBand*sizeof(uchar);
  memcpy(data  , trg, n);
  memcpy(data+n, src, nPixel*sizeof(uchar));
  Free(trg); 
  Free(src); 
  return data;
}

//------------------------------------------

int imreadGif(char* filename, int nImage, bool verbose,
              uchar** data, int &nRow, int &nCol, int &nBand,
              int ColorMap[255], int &Transparent, char** Comment)
{
  bool interlace;
  uchar buffer[256], c, *cube=0, *image=0;
  int Width, Height, i, iImage, ret, DelayTime, stats, done, n, m, nColMap=0, filesize=0;
  char version[7], fname[256], *str, *p, *comment=0;
  
  *data=NULL;
  *Comment=NULL;
  Width=Height=nRow=nCol=nBand=0; 
  ret=Transparent=-1;
  str=fname;
  strcpy(fname,filename);
  i = (int) strlen(fname);
  if (fname[i-4]=='.') strcpy(strrchr(fname,'.'),".gif");
  FILE *fp = fopen(fname,"rb");
  if (fp==0) return -1;     

  //====================================================
  // GIF Signature, Screen Descriptor & Global Color Map
  //====================================================
  if (!fread(version, 6, 1, fp)) return -2;    // Read Header
  version[6] = '\0';
  if ((strcmp(version, "GIF87a") != 0) && (strcmp(version, "GIF89a") != 0)) return -2;
  if (!fread(buffer, 7, 1, fp)) return -3;     // Read Screen Descriptor
  if(verbose) print("GIF image header\n");
  i = ReadColorMap(fp, buffer[4], ColorMap);   // Read Global Colormap
  if (i==0) return -3;
  if (i==2) nColMap++;
  if(verbose) 
    if(i==2) print("Global colormap with %i colors \n", 2<<(buffer[4]&0x07));
    else     print("No global colormap provided\n");
  filesize += 6 + 7 + 3*256;

  //====================================================
  // Raster Data of encoded images and Extention Blocks
  //====================================================
  iImage = stats = done = 0;
  while(!stats && !done) {
    c = fgetc(fp);
    switch(c) {
      case EOF:  stats=3; break;                        // unexpected EOF
      
      case 0x3b:                                        // GIF terminator ";"
        done =1; 
        if(verbose) print("GIF Terminator\n");
        break;                                       

      case 0x21:                                        // GIF Extention Block introducer
        c = fgetc(fp);
        switch (c) {
          case EOF : stats=3; break;                    // unexpected EOF
          case 0xf9:                                    // "Graphic Control Extension" 
            n = GetDataBlock(fp, buffer);               // block is of size 4
            if (n==4) {                                 // block has to be of size 4
              DelayTime = getint(buffer+1);
              if ((buffer[0] & 0x1) != 0) Transparent = buffer[3];
              if(verbose) 
                print("Graphic Control Extension (delay=%i transparent=%i)\n",
                     DelayTime, Transparent);
            }
            while (GetDataBlock(fp, buffer) != 0);      // look for block terminator
            break;
          case 0xfe:                                    // "Comment Extension" 
            m = (comment ? strlen(comment) : 0);
            while ((n=GetDataBlock(fp, buffer)) != 0) { // look for block terminator
               p = Calloc(m+n+1,char);
               if(m>0) {                                // if there was a previous comment than whey will be concatinated
                 memcpy(p,comment,m);
                 free(comment);
               }
               comment = p;
               strncpy(comment+m, (char*) buffer, n);
               m+=n;
               comment[m]=0;
            }
            if(verbose) print("Comment Extension\n");
            break;
          case 0xff:                                    // "Software Specific Extension" most likelly NETSCAPE2.0 
            while (GetDataBlock(fp, buffer) != 0);      // look for block terminator
            if(verbose) print("Animation Extension\n");
            break;
          case 0x01:                                    // "Plain Text Extension" 
            while (GetDataBlock(fp, buffer) != 0);      // look for block terminator
            if(verbose) print("Plain Text Extension (ignored)\n");
            break;
          default:                                      // Any other type of Extension
            while (GetDataBlock(fp, buffer) != 0);      // look for block terminator
            if(verbose) print("Unknown Extension %i\n", c);
            break;
        }
        break;
      
      case 0x2c:  // Image separator found
        //====================================
        // Image Descriptor
        //====================================
        if (!fread(buffer, 9, 1, fp)) {stats=3; break;} // unexpected EOF
        Width     = getint(buffer+4); // Byte 6&7: Read the Image width
        Height    = getint(buffer+6); // Byte 8&9: Read the Image height
        interlace = ((buffer[8]&0x40)==0x40);
        if(verbose) print("Image [%i x %i]: ", Height, Width);
        if (!nImage && nBand>0 && (nRow!=Height || nCol!=Width)) {stats=5; break;}

        //=============================================
        // Local Color Map & Raster Data (LZW encrypted)
        //=============================================
        i = ReadColorMap(fp, buffer[8], ColorMap, nColMap*nImage); // Read local Colormap
        if (i==0) {stats=3; break;} // EOF found during reading local colormap
        if (i==2) nColMap++;
        if(image) Free(image);
        image = Calloc(Height*Width, uchar);
        ret   = DecodeLZW(fp, image, Height*Width);
        if(interlace) InterlaceShuffle(image, Width, Height, true); // rearrange rows to undo interlace 
        if (!nImage) {              // save all bands
          if (!nBand) cube = image; // first image
          else cube = append(cube, image, Height*Width, nBand);
          nBand++;
        } else {                    // replace each image with new one
          if(cube) Free(cube);
          cube  = image;
          nBand = 1;
        }
        image = 0;
        nRow = Height;
        nCol = Width;
        if (ret==0) {stats=4; break;} // DecodeLZW exit without finding file terminator
        else filesize += ret+10;
        if (++iImage==nImage) done=1;
        if(verbose) print("%i bytes \n", ret);
        if(verbose && i==2) print("Local colormap with %i colors \n", 2<<(buffer[4]&0x07));
        break;

      default:
        if(verbose) print("Unexpected character %c (%i)\n", c, c);
        stats=4; 
        break;
    }

  } // end while
  if(verbose) print("\n");
  *Comment = comment;
  *data = cube;
  nRow  = Height;
  nCol  = Width;
  if (nImage==0 && nColMap>1) stats += 6;
  if (stats) ret = -stats; // if no image than save error #
  return filesize;
}

//==============================================================
  
void imwritegif(char** filename, 
              int* Data, int *ColorMap, int *param, char** comment)
{
  int i, nPixel = param[0]*param[1]*param[2];
  bool Interlace = (param[6]!=0);

  uchar* data = Calloc(nPixel, uchar);
  for(i=0; i<nPixel; i++) data[i] = Data[i]&0xff;
  param[7] = imwriteGif(*filename, data, param[0], param[1], param[2], param[3],
    ColorMap, Interlace, param[4], param[5], *comment);
  Free(data);
}

#ifndef  TEST_ALG

  SEXP imreadgif(SEXP filename, SEXP NImage, SEXP Verbose)
  { // The only R specific function
    int i, j, nPixel, nRow, nCol, nBand, ColorMap[256]; 
    int transparent, success, *ret, nImage, verbose;
    char *fname, *comment;
    uchar* data=0;
    SEXP Ret;

    // initialize data 
    nRow=nCol=nBand=transparent=0;
    comment = NULL;
    nImage  = asInteger(NImage);    
    verbose = asInteger(Verbose);    
    fname   = CHAR(STRING_ELT(filename, 0));
    success = imreadGif(fname, nImage, (bool) verbose, &data, nRow, nCol, 
              nBand, ColorMap, transparent, &comment); 
    nPixel  = nRow*nCol*nBand;
    PROTECT(Ret = allocVector(INTSXP, 9+256+nPixel));
    ret     = (int*) INTEGER(Ret);  /* get pointer to R's Ret */
    ret[0]  = nRow;       // pack output into array of integers
    ret[1]  = nCol;
    ret[2]  = nBand;
    ret[3]  = transparent;
    ret[4]  = success;
    j=9;
    for(i=0; i<256   ; i++) ret[j++] = ColorMap[i];
    for(i=0; i<nPixel; i++) ret[j++] = data[i];
    Free(data);
    if(comment && strlen(comment)) // if comment was found than pack it too
      setAttrib(Ret, install("comm"), mkString(comment));
    if(comment) Free(comment);
    UNPROTECT(1);
    return Ret;
  }

#else

int main()
{
  bool interlace;
  int nRow, nCol, nBand, ColorMap[256], transparent, *Data=0, DelayTime, nImage, succes, n;
  char *Comment=0, str[256];
  uchar *data=0;

  interlace = 0;
  DelayTime = 0;
  nImage    = 0;
  succes = imreadGif ("bats.gif", nImage, (bool) 1, &data, nRow, nCol, nBand, ColorMap, transparent, &Comment);
  printf("Image read = [%i x %i x %i]: %i\n",nRow, nCol, nBand, succes);
  if (1) {
    n = nRow*nCol;
    strcpy(str, "hello world");
    succes = imwriteGif("tmp.gif", data, nRow, nCol, nBand, ColorMap, interlace, transparent, DelayTime, str);
    printf("Image written = [%i x %i x %i]: %i\n",nRow, nCol, nBand, succes);
  }
  printf("Press any key\n");
  getchar();
  return 0;
}

#endif


} //end of extern


