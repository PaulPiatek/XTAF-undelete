#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <netinet/in.h>

char DELETED = 0xE5;
char AFILE = 0x00;
char AFILERO= 0x01;
char AFILEHI = 0x02;
char AFILESY = 0x04;
char AFILEAR = 0x20;
char EMPTY_FIELD = 0x00;
char FOLDER = 0x10;
char PLACEHOLDER = 0xFF;
off_t XTAF_OFFSET = 0x130eb0000ll;
off_t FAT_OFFSET = 0x130eb1000ll;
off_t BEGIN_XTAF_DIR = 0x131225000ll;
off_t BEGIN_MY_DIR = 0x131231040ll;
off_t FIRST_CLUSTER = 0x131229000ll;
off_t CLUSTER_SIZE = 0x4000ll;
int  FAT_CHAIN_ENTRY_SIZE = 4;

typedef struct
{
  char sizeOfFileName;
  char attribute;
  char fileName[42];
  char firstCluster[4];
  unsigned int  firstClusterInt;
  char fileSize[4];
  unsigned int  fileSizeInt;
  char dates[12];
  off_t offset;
} fatEntry;

int isFile(char c)
{
  return ( c == AFILE || c == AFILERO || c == AFILEHI || c == AFILESY || c == AFILEAR);
}

unsigned int char4toint(char* buf)
{
  char tmp[4];
  int i;
  tmp[0] = buf[3];
  tmp[1] = buf[2];
  tmp[2] = buf[1];
  tmp[3] = buf[0];
  i = *(unsigned int*)tmp;
  return i;
}

void inttochar4(int value, char* buf)
{
  /*buf[3] = *((char*)&value);   // hehehe
	buf[2] = *(((char*)&value)+1);
	buf[1] = *(((char*)&value)+2);
	buf[0] = *(((char*)&value)+3);*/
	value = htonl(value);
	memcpy(buf, &value, 4);

}

fatEntry buf2fe(char* buf, off_t offset)
{
  fatEntry fe;
  fe.sizeOfFileName = buf[0];
  fe.attribute = buf[1];
  int i;
  for (i=0; i<42; i++)
  {
    if (buf[i+2] != PLACEHOLDER)
      fe.fileName[i] = buf[i+2];
    else
      fe.fileName[i] = EMPTY_FIELD;
  }
  for (i=0; i<4; i++)
    fe.firstCluster[i] = buf[i+44];
  fe.firstClusterInt = char4toint(fe.firstCluster);
  for (i=0; i<4; i++)
    fe.fileSize[i] = buf[i+48];
  fe.fileSizeInt = char4toint(fe.fileSize);
  for (i=0; i<12; i++)
    fe.dates[i] = buf[i+52];
  fe.offset = offset;
  return fe;
}

off_t getNext(off_t offset, FILE* file)
{
  off_t actualPos = ftello(file);
  fseeko(file, FAT_OFFSET+(offset*FAT_CHAIN_ENTRY_SIZE), SEEK_SET);
  char next[FAT_CHAIN_ENTRY_SIZE];
  fread(next, FAT_CHAIN_ENTRY_SIZE, 1, file);
  fseeko(file, actualPos, SEEK_SET);
  return char4toint(next);
}

int checkConsistencyAndRepair(fatEntry fe, FILE* file, int repair)
{
  //check for 0-byte file
  if (fe.fileSizeInt == 0)
  {
    return 0;
  }


  off_t actualPos = ftello(file);
  //printf("fatFirstCluster: %x\n", fe.firstClusterInt);
  int numClusters = (fe.fileSizeInt / CLUSTER_SIZE);
  if (fe.fileSizeInt % CLUSTER_SIZE != 0)
    numClusters++;
  //printf ("\nnumCluster: %d\n", numClusters);
  int i = 0;
  // regular - try to follow table...
  off_t next;
  int cluster = fe.firstClusterInt;
  for (i = 0; i < numClusters; i++)
  {
    //printf("pos: %llx\n", BEGIN_XTAF_DIR + cluster*CLUSTER_SIZE);
    next = getNext(cluster, file);
    //printf("nextCluster: %llX\n", next);
    cluster = next;
    if (next != 0xFFFFFFFF && next != 0)
      fseeko(file, FAT_OFFSET + (next * CLUSTER_SIZE), SEEK_SET);
    else
      break;
  }
  if (i == numClusters-1) // everything okay
  {
    fseeko(file, actualPos, SEEK_SET);
    return 0;
  }

  // check if we can recreate
  for (i = 0; i < numClusters; i++)
  {
    fseeko(file, FAT_OFFSET + (fe.firstClusterInt*FAT_CHAIN_ENTRY_SIZE) + (i * FAT_CHAIN_ENTRY_SIZE), SEEK_SET);
    //printf ("search at %llx", FAT_OFFSET + (fe.firstClusterInt*FAT_CHAIN_ENTRY_SIZE) + (i * FAT_CHAIN_ENTRY_SIZE));
    char next[FAT_CHAIN_ENTRY_SIZE];
    fread(next, FAT_CHAIN_ENTRY_SIZE, 1, file);
    //printf("%x, %x, %x, %x", next[0], next[1], next[2], next[3]);
    if (char4toint(next) != 0)
    {
      //printf("break at %llx", FAT_OFFSET + (fe.firstClusterInt*FAT_CHAIN_ENTRY_SIZE) + (i * FAT_CHAIN_ENTRY_SIZE));
      break;
    }
  }
  //printf ("  IIIII %d  ", i);
  if (i == numClusters)
  {
    // we can recreate

    // repair.....
    if (repair == 1 && fe.sizeOfFileName != DELETED)
    {
      int i;
      for (i = 0; i < numClusters; i++)
      {
        fseeko(file, FAT_OFFSET + (fe.firstClusterInt*FAT_CHAIN_ENTRY_SIZE) + (i * FAT_CHAIN_ENTRY_SIZE), SEEK_SET);
        int nextEntryValue;
        if (i < (numClusters - 1))
          nextEntryValue = fe.firstClusterInt + i + 1; // +1 because we need the next entry
        else
          nextEntryValue = 0xFFFFFFFF;
        char nextEntry[4];
        inttochar4(nextEntryValue, nextEntry);
        printf("at position %llx i would write %2x, %2x, %2x, %2x (%s)\n", (FAT_OFFSET + (fe.firstClusterInt*FAT_CHAIN_ENTRY_SIZE) + (i * FAT_CHAIN_ENTRY_SIZE)), nextEntry[0], nextEntry[1], nextEntry[2], nextEntry[3], nextEntry);
        if (repair == 1)
          fwrite(nextEntry, 1, sizeof(nextEntry), file);
      }
      printf(" repaired file ");
    }

    fseeko(file, actualPos, SEEK_SET);
    return 1;
  }
  else
  {
    // booooh
    if (isFile(fe.attribute) && fe.sizeOfFileName != DELETED)
    {
      printf(" i would mark this dir-entry at pos %llx as deleted(%x) ", fe.offset, DELETED);
      fseeko(file, fe.offset, SEEK_SET);
      if (repair == 1)
        fputc(DELETED, file);
        //fwrite(DELETED, 1, sizeof(DELETED), file);
    }
    fseeko(file, actualPos, SEEK_SET);
    return 2;
  }
}

void printInfo(fatEntry fe, int depth)
{
  int maxdepth = 10;
  int append = maxdepth-depth;
  int i;
  for (i = 0; i < depth; i++)
    printf("~");
  if (fe.attribute == AFILE)
    printf("FILE    ");
  else if (fe.attribute == FOLDER)
    printf("FOLDER  ");
  else if (fe.attribute == AFILERO)
    printf("READONLY");
  else if (fe.attribute == AFILEHI)
    printf("HIDDEN  ");
  else if (fe.attribute == AFILESY)
    printf("SYSTEM  ");
  else if (fe.attribute == AFILEAR)
    printf("ARCHIVE ");
  for (i = 0; i < append; i++)
      printf(" ");
  if (fe.sizeOfFileName == DELETED)
    printf(" %-42s [deleted]", fe.fileName);
  else
    printf(" %-52s", fe.fileName);

  printf("    cluster: %8X", fe.firstClusterInt);
  printf("    size: %16d bytes ", fe.fileSizeInt);

}

void work(off_t offset, FILE* file, int depth, int repair)
{
  char buffer[64];
  fseeko(file, BEGIN_XTAF_DIR + offset, SEEK_SET);
  off_t i = 0;
  int tmp = 0;
  while(1)
  {
    for (i=0; i<64; i++ )
    {
      char c = fgetc(file);
      buffer[i] = c;
      tmp++;
    }

    fatEntry fe = buf2fe(buffer, BEGIN_XTAF_DIR + offset + tmp - 64);

    if (fe.attribute == FOLDER || isFile(fe.attribute))
    {
      printInfo(fe, depth);
      if (isFile(fe.attribute))
      {
        int check = checkConsistencyAndRepair(fe, file, repair);
        if (check == 0)
        {
          printf("everything okay\n");
        }
        else if (check == 1)
        {
          printf("may be recreated\n");
        }
        else if (check == 2)
        {
          printf("corrupt (possibly overwritten)\n");
        }
        else if (check == 3)
        {
          printf("recreated file\n");
        }
      }
      else
      {
        printf("\n");
      }
    }
    if (fe.attribute == FOLDER)
    {
      work((off_t)fe.firstClusterInt*CLUSTER_SIZE, file, depth+1, repair);
      fseeko(file, BEGIN_XTAF_DIR + offset + tmp, SEEK_SET);
    }

    if (tmp == 0x4000) // check fat-table for cluster chain
    {
      tmp = 0;
      off_t nextCluster = getNext(offset / CLUSTER_SIZE, file);
      if (nextCluster < 2 || nextCluster > 0xffffffef )
        return;
      fseeko(file, BEGIN_XTAF_DIR + (nextCluster*CLUSTER_SIZE), SEEK_SET);
      offset = nextCluster * CLUSTER_SIZE;
    }
    else if (fe.attribute != FOLDER && !isFile(fe.attribute))
      return;
  }
}

int main (int argc, char ** argv)
{

  int repair = 0;

  if (argc < 2)
  {
    printf("usage: xtaf device/filename [repair]");
    return 1;
  }
  else
  {
    printf("File: %s\n", argv[1]);
  }
  printf("argv[2]: %s\n", argv[2]);
  printf("argc: %d\n", argc);

  if (argc == 3 && 0 == strcmp(argv[2], "repair"))
  {
    printf("Really repair? y/N ");
    char c = getchar();
    if (c == 'y')
    {
      repair = 1;
      printf("\nOkay, will repair data \n");
    }
  }
  else
  {
    printf("\nWill not repair data \n");
  }
  //FILE * file = fopen("D:\\Users\\Paul\\XboxBackup.bin", "rb");
  //FILE * file = fopen("f:\\XboxBackupRep.bin", "rb");
  FILE * file;
  if (repair == 0)
    file = fopen(argv[1], "rb");
  if (repair == 1)
    file = fopen(argv[1], "rb+");
  //FILE * fout = fopen("f:\\export0.bin", "wb");



  //work(FIRST_CLUSTER-BEGIN_XTAF_DIR, file, 0, repair);
  work(BEGIN_MY_DIR-BEGIN_XTAF_DIR, file, 0, repair);




  //off_t off = 0x154E; // offset from BEGIN_XTAF
  //int length = 0x002CD000; // length in bytes

  /*
  off_t offset = (off * CLUSTER_SIZE) + BEGIN_XTAF;

  fseeko(file, offset, SEEK_SET);

  int i;
  for (i = 0; i < length; i++)
  {
    char c = fgetc(file);
    fputc(c, fout);
  }
  */
  fflush(file);
  fclose(file);
  //fflush(fout);
  //fclose(fout);
  return 0;

}
