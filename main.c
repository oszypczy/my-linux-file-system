#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <time.h>

#define BLOCK_SIZE 2048   /* Rozmiar bloku danych */
#define MAX_NAME_SIZE 64  /* Maksymalna dlugosc nazwy pliku */

struct superblock{
  unsigned int diskSize;          /* Calkowity rozmiar wirtualnego dysku */
  char name[MAX_NAME_SIZE];       /* Nazwa dysku */
  unsigned int fatBlockSize;      /* Rozmiar bloku tablicy alokacji plików */
  unsigned int rootdirBlockSize;  /* Rozmiar bloku katalogu glownego */
  unsigned int blockSize;         /* Rozmiar bloku danych */
};

struct vfs{
  FILE * file;                      /* Wskaźnik na plik dysku */
  unsigned int blockAmount;         /* Ilość bloków danych */
  struct dirEntry * rootDirectory;  /* Tablica z informacjami o plikach */
};

struct dirEntry{
  char name [MAX_NAME_SIZE];    /* Nazwa pliku */
  unsigned int size;            /* Rozmiar pliku */
  time_t create;                /* Czas utworzenia */
  time_t modify;                /* Czas ostatniej modyfikacji */
  int firstBlock;               /* Numer pierwszego bloku danych */
  unsigned int valid;           /* Czy wpis jest aktualny */
};

/* Deklaracje funkcji */
int createVFS(char * name, unsigned int size);
struct vfs * openVFS(char * name);
void closeVFS(struct vfs * vfs);
int removeVFS(char * name);
int copyIntoVD(char * name, char * sourceFileName, char * destFileName);
int copyFromVD(char * name, char * sourceFileName, char * destFileName);
int removeFromVD(char * name, char *fileName);
int viewFiles(char * name);
int viewInfo(char * name);
char * getDate(time_t time);
int calculateLastBlock(int firstBlock, int size);
void printInstructions(char * name);
unsigned int handleFunction(char * name, char * func, char * argv[], int interFlag);
void handleError(char * name, unsigned int error);
char * printUserMenu(void);
char * getUserInput(char * prompt);


int main(int argc, char * argv[]){
  char * vfsName;
  char * func;
  unsigned int error;
  int interFlag;

  if (argc < 3 || argc > 5)
  {
    printInstructions(argv[0]);
    return 1;
  }

  vfsName = argv[1];
  func = argv[2];
  interFlag = 0;

  do
  {
    if (strcmp(func, "-interactive") == 0 || strcmp(func, "-i") == 0 || interFlag) 
    {
      interFlag = 1;
      func = printUserMenu();
    }

    /* Obsługa funkcji */
    error = handleFunction(vfsName, func, argv, interFlag);

    /* Obsługa kodów błędów */
    if (error != 0) handleError(argv[0], error);

  } while (interFlag && error != 10);

  return 0;
}

/* Funkcja createVFS tworzy wirtualny dysk (Virtual File System) o zadanej nazwie i rozmiarze.
 * Zwraca 0 w przypadku sukcesu, 1 w przypadku błędu otwarcia pliku. */
int createVFS(char * name, unsigned int size){
  FILE * file;
  unsigned int blockAmount;
  struct superblock sb;
  struct dirEntry * rootDirectory;
  struct vfs * vfs;
  char * zeros;
  int * fatTable;
  int i;

  /* Sprawdź, czy rozmiar dysku jest wystarczający */
  if(size < BLOCK_SIZE + sizeof(struct superblock) + 64 * sizeof(int) + sizeof(struct dirEntry)){
    return 7;
  }

  /* Oblicz ilość bloków danych na dysku */
  blockAmount = (size - sizeof(sb))/(sizeof(struct dirEntry) + BLOCK_SIZE + sizeof(int));

  /* Zainicjalizuj tablicę 'zeros' zerami i zapisz ją na dysku */
  zeros = malloc(size * sizeof(char));
  memset(zeros, 0, sizeof(zeros));

  /* Otwórz plik dysku */
  file = fopen(name, "w+");
  if(!file) return 1;
  fwrite(zeros, 1, size, file);

  /* Przygotuj miejsce na tablicę alokacji plików (FAT) i katalog główny */
  rootDirectory = malloc(sizeof(struct dirEntry) * blockAmount);
  fatTable = malloc(sizeof(int) * blockAmount);

  /* Inicjalizuj strukturę superblock */
  sb.diskSize = size;
  strncpy(sb.name, name, MAX_NAME_SIZE);
  sb.fatBlockSize = sizeof(int) * blockAmount;
  sb.rootdirBlockSize = sizeof(struct dirEntry) * blockAmount;
  sb.blockSize = BLOCK_SIZE;

  /* Inicjalizuj tablicę alokacji plików (FAT) */
  for(i=0; i<blockAmount; i++){
    fatTable[i] = -2;
  }

  /* Zapisz strukturę superblock i tablicę alokacji plików (FAT) na dysku */
  fseek(file, 0, 0);
  fwrite(&sb, sizeof(sb), 1, file);
  fseek(file, sizeof(sb), 0);
  fwrite(fatTable, sizeof(int), blockAmount, file);

  /* Inicjalizuj katalog główny */
  for(i=0; i<blockAmount; i++){
    strcpy(rootDirectory[i].name, "");
    rootDirectory[i].size = 0;
    rootDirectory[i].firstBlock = -1;
    rootDirectory[i].valid = 0;
  }

  /* Inicjalizuj strukturę VFS i zwolnij pamięć */
  vfs = malloc(sizeof(struct vfs));
  vfs->file = file;
  vfs->blockAmount = blockAmount;
  vfs->rootDirectory = rootDirectory;

  /* Zwolnij pamięć i zamknij pliki */
  free(zeros);
  free(fatTable);
  closeVFS(vfs);

  printf("Virtual disk \"%s\" created successfully\n\n", name);

  return 0;
}

/* Funkcja openVFS otwiera istniejący wirtualny dysk o zadanej nazwie.
 * Zwraca wskaźnik do struktury VFS w przypadku sukcesu 
 * lub NULL w przypadku błędu otwarcia pliku albo nieprawidłowego formatu dysku. */
struct vfs * openVFS(char * name){
  FILE * file;
  unsigned int blockAmount;
  unsigned int size;
  struct superblock sb;
  struct dirEntry * rootDirectory;
  struct vfs * vfs;
  int i;
  int result;

  /* Otwórz plik dysku w trybie odczytu i zapisu */
  file = fopen(name, "r+");
  if(!file) return NULL;

  /* Ustal rozmiar pliku (dysku) */
  fseek(file, 0, 2);
  size = ftell(file);

  /* Przeczytaj superblock z początku pliku */
  fseek(file, 0, 0);
  result = fread(&sb, sizeof(sb), 1, file);

  /* Sprawdź, czy odczytano poprawnie superblock i czy rozmiar pliku jest wystarczający */
  if(result == 0 || size < sizeof(struct superblock) || sb.diskSize != size){
  	fclose(file);
  	return NULL;
  }

  /* Oblicz ilość bloków danych na dysku */
  blockAmount = (sb.diskSize - sizeof(sb))/(sizeof(struct dirEntry) + BLOCK_SIZE + sizeof(int));
  rootDirectory = malloc(blockAmount * sizeof(struct dirEntry));

  /* Przesuń kursor pliku na pozycję, gdzie zapisana jest tablica alokacji plików (FAT) */
  fseek(file, sizeof(sb)+sizeof(int)*blockAmount, 0);
  result = fread(rootDirectory, sizeof(struct dirEntry), blockAmount, file);

  /* Sprawdź, czy odczytano poprawnie katalog główny */
  if(result == 0){
  	free(rootDirectory);
  	fclose(file);
  	return NULL;
  }

  /* Inicjalizuj strukturę VFS i zwróć wskaźnik */
  vfs = malloc(sizeof(struct vfs));
  vfs->file = file;
  vfs->blockAmount = blockAmount;
  vfs->rootDirectory = rootDirectory;

  printf("Virtual disk \"%s\" opened successfully\n\n", name);

  return vfs;
}

/* Funkcja closeVFS zamyka wirtualny dysk, zapisując aktualne informacje o katalogu głównym na dysku.
 * Zwolnienie zaalokowanej pamięci i zamknięcie pliku. */
void closeVFS(struct vfs * vfs){
  
  /* Przesuń kursor pliku na pozycję, gdzie zapisana jest tablica alokacji plików (FAT) */
  fseek(vfs->file, sizeof(struct superblock)+sizeof(int)*vfs->blockAmount, 0);
  
  /* Zapisz katalog główny na dysku */
  fwrite(vfs->rootDirectory, sizeof(struct dirEntry), vfs->blockAmount, vfs->file);
  
  /* Zamknij plik dysku */
  fclose(vfs->file);
  
  /* Zwolnij zaalokowaną pamięć dla katalogu głównego i struktury VFS */
  free(vfs->rootDirectory);
  free(vfs);

  /* Ustaw wskaźnik na NULL, aby uniknąć dostępu do zwolnionej pamięci */
  vfs = NULL;
}

/* Funkcja removeVFS usuwa wirtualny dysk o zadanej nazwie.
 * Zwraca 0 w przypadku sukcesu, a 1 w przypadku błędu. */
int removeVFS(char * name){
  int success;
  
  /* Usuń plik dysku o zadanej nazwie */
  success = remove(name);
  
  /* Zwróć odpowiednią wartość w zależności od sukcesu operacji */
  if (success != 0) 
    return 2;
  else 
    printf("Virtual disk \"%s\" removed successfully\n\n", name);
    return 0;
}


/* Funkcja copyIntoVD kopiuje plik z systemu plików rzeczywistego do wirtualnego dysku.
 * Zwraca 0 w przypadku sukcesu, a 1 w przypadku błędu.
 * Parametry:
 * - name: nazwa wirtualnego dysku
 * - sourceFileName: nazwa pliku do skopiowania z systemu plików rzeczywistego
 * - destFileName: nazwa pliku docelowego na wirtualnym dysku */
int copyIntoVD(char * name, char * sourceFileName, char * destFileName){
  FILE * file;
  struct vfs * vfs;
  unsigned int sourceFileSize;
  unsigned int requiredBlocks;
  unsigned int blockIndex;
  unsigned int i;
  unsigned int tmp;
  unsigned int * dirEntrys;
  unsigned int position;
  int modify;
  int result;
  char data[BLOCK_SIZE];
  int * fatTable;
  time_t tmpCreate;
  int size;

  vfs = openVFS(name);

  /* Sprawdź poprawność otwarcia dysku oraz czy nazwa pliku docelowego nie jest pusta */
  if (!vfs || strcmp(destFileName, "") == 0) return 3;

  /* Modyfikacja istniejącego pliku, jeżeli istnieje o tej samej nazwie */
  modify = -1;
  for(i=0; i<vfs->blockAmount; i++){
	   if(vfs->rootDirectory[i].valid == 1 && strcmp(vfs->rootDirectory[i].name, destFileName) == 0) {
       modify = i;
       break;
     }
  }

  if(modify != -1){
    tmpCreate = vfs->rootDirectory[modify].create;
    closeVFS(vfs);
    removeFromVD(name, destFileName);
    vfs = openVFS(name);
    if (!vfs) return 3;
  }

  /* Otwórz plik do skopiowania z systemu plików rzeczywistego */
  file = fopen(sourceFileName, "r+");
  if(!file) return 1;

  /* Przygotuj tablicę alokacji plików (FAT) */
  fatTable = malloc(sizeof(int)*vfs->blockAmount);

  /* Ustal rozmiar pliku źródłowego */
  fseek(file, 0, 2);
  sourceFileSize = ftell(file);

  /* Przeczytaj aktualną tablicę alokacji plików (FAT) z wirtualnego dysku */
  fseek(vfs->file, sizeof(struct superblock), 0);
  result = fread(fatTable, sizeof(int), vfs->blockAmount, vfs->file);
  if(result == 0){
    fclose(file);
    return 4;
  }

  /* Przygotuj się do ponownego odczytu od początku pliku źródłowego */
  fseek(file, 0, 0);
  requiredBlocks = 1;
  tmp = sourceFileSize;
  while(tmp > BLOCK_SIZE){
  	requiredBlocks++;
  	tmp-=BLOCK_SIZE;
  }

  /* Przygotuj tablicę z indeksami wolnych bloków danych */
  dirEntrys = malloc(requiredBlocks * sizeof(unsigned int));
  blockIndex = 0;
  for(i=0; i<vfs->blockAmount; i++){
  	if(!vfs->rootDirectory[i].valid){
  		dirEntrys[blockIndex] = i;
  		blockIndex++;
  	}
  	if(blockIndex == requiredBlocks) break;
  }

  /* Sprawdź, czy znaleziono wystarczającą ilość wolnych bloków danych */
  if(blockIndex < requiredBlocks){
  	free(dirEntrys);
  	fclose(file);
	  return 5;
  }

  /* Przeczytaj dane z pliku źródłowego i zapisz je na wirtualnym dysku */
  for(i=0; i<requiredBlocks; i++){
    size = fread(data, 1, sizeof(data), file);
    vfs->rootDirectory[dirEntrys[0]].size += size;
    vfs->rootDirectory[dirEntrys[i]].valid = 1;
    vfs->rootDirectory[dirEntrys[i]].size = size;

  	if(i==0) {
    	strncpy(vfs->rootDirectory[dirEntrys[i]].name, destFileName, MAX_NAME_SIZE);
      vfs->rootDirectory[dirEntrys[i]].firstBlock = dirEntrys[i];
      vfs->rootDirectory[dirEntrys[i]].modify = time(NULL);
      if(modify == -1) vfs->rootDirectory[dirEntrys[i]].create = vfs->rootDirectory[dirEntrys[i]].modify;
      else vfs->rootDirectory[dirEntrys[i]].create = tmpCreate;

    }

  	if(i < requiredBlocks - 1){
      fatTable[dirEntrys[i]] = dirEntrys[i+1];
    }

    if(i == requiredBlocks - 1) {
      fatTable[dirEntrys[i]] = -1;
    }

  	position = sizeof(struct superblock) + (sizeof(int) + sizeof(struct dirEntry)) * vfs->blockAmount + BLOCK_SIZE * dirEntrys[i];

  	fseek(vfs->file, position, 0);
  	fwrite(data, 1, size, vfs->file);
  }

  /* Zapisz zmodyfikowaną tablicę alokacji plików (FAT) na dysku */
  fseek(vfs->file, sizeof(struct superblock), 0);
  fwrite(fatTable, sizeof(int), vfs->blockAmount, vfs->file);

  /* Zwolnij pamięć i zamknij pliki */
  free(fatTable);
  free(dirEntrys);
  fclose(file);
  closeVFS(vfs);

  printf("File \"%s\" copied successfully\n\n", sourceFileName);

  return 0;
}

/* Funkcja copyFromVD kopiuje plik z wirtualnego dysku do systemu plików rzeczywistego.
 * Zwraca 0 w przypadku sukcesu, a 1 w przypadku błędu.
 * Parametry:
 * - name: nazwa wirtualnego dysku
 * - sourceFileName: nazwa pliku do skopiowania z wirtualnego dysku
 * - destFileName: nazwa pliku docelowego w systemie plików rzeczywistego */
int copyFromVD(char * name, char * sourceFileName, char * destFileName){
  FILE * file;
  struct vfs * vfs;
  unsigned int position;
  unsigned int i;
  int tmp;
  int result;
  char data[BLOCK_SIZE];
  int * fatTable;
  unsigned int tempSize;
  unsigned int size;

  vfs = openVFS(name);

  /* Sprawdź poprawność otwarcia dysku oraz czy nazwa pliku docelowego nie jest pusta */
  if (!vfs || strcmp(destFileName, "") == 0) return 3;

  /* Otwórz plik docelowy w trybie zapisu */
  file = fopen(destFileName, "w+");
  if(!file) return 1;

  /* Przygotuj tablicę alokacji plików (FAT) */
  fatTable = malloc(sizeof(int)*vfs->blockAmount);

  /* Przeczytaj aktualną tablicę alokacji plików (FAT) z wirtualnego dysku */
  fseek(vfs->file, sizeof(struct superblock), 0);
  result = fread(fatTable, sizeof(int), vfs->blockAmount, vfs->file);
  if(result == 0) return 4;

  /* Przeszukaj katalog główny w celu znalezienia pliku o zadanej nazwie */
  tmp = -1;
  for(i=0; i<vfs->blockAmount; i++){
  	if(vfs->rootDirectory[i].valid && vfs->rootDirectory[i].firstBlock >= 0 && strcmp(vfs->rootDirectory[i].name, sourceFileName) == 0){
  		tmp = i;
  		break;
  	}
  }

  /* Nie znaleziono pliku o danej nazwie */
  if(tmp == -1) {
  	fclose(file);
  	return 1;
  }
  
  /* Przeczytaj dane z wirtualnego dysku i zapisz je do pliku docelowego */
  size = vfs->rootDirectory[tmp].size;
  while(tmp >= 0){
    tempSize = size;
    if(size > BLOCK_SIZE) {
      size-=BLOCK_SIZE;
      tempSize = BLOCK_SIZE;
    }
  	position = sizeof(struct superblock) + (sizeof(int) + sizeof(struct dirEntry)) * vfs->blockAmount + BLOCK_SIZE * tmp;
  	fseek(vfs->file, position, 0);
  	if(fread(data, 1, tempSize, vfs->file) == 0){
  		fclose(file);
  		return 1;
  	}
  	fwrite(data, 1, tempSize, file);
    tmp = fatTable[tmp];
  }

  /* Zwolnij pamięć i zamknij pliki */
  free(fatTable);
  fclose(file);
  closeVFS(vfs);

  printf("File \"%s\" copied successfully\n\n", sourceFileName);

  return 0;
}

/* Funkcja removeFromVD usuwa plik o zadanej nazwie z wirtualnego dysku.
 * Zwraca 0 w przypadku sukcesu, a 1 w przypadku błędu.
 * Parametry:
 * - name: nazwa wirtualnego dysku
 * - fileName: nazwa pliku do usunięcia z wirtualnego dysku */
int removeFromVD(char * name, char *fileName){
  struct vfs * vfs;
  unsigned int i;
  int tmp;
  int temp;
  int result;
  char data[BLOCK_SIZE];
  int * fatTable;

  vfs = openVFS(name);

  /* Sprawdź poprawność otwarcia dysku oraz czy nazwa pliku nie jest pusta */
  if(!vfs || strcmp(fileName, "") == 0) return 3;

  /* Przygotuj tablicę alokacji plików (FAT) */
  fatTable = malloc(sizeof(int) * vfs->blockAmount);

  /* Przeczytaj aktualną tablicę alokacji plików (FAT) z wirtualnego dysku */
  fseek(vfs->file, sizeof(struct superblock), 0);
  result = fread(fatTable, sizeof(int), vfs->blockAmount, vfs->file);
  if(result == 0) return 4;

  /* Przeszukaj katalog główny w celu znalezienia pliku o zadanej nazwie */
  tmp = -1;
  for(i=0; i<vfs->blockAmount; i++){
  	if(vfs->rootDirectory[i].valid && strcmp(vfs->rootDirectory[i].name, fileName) == 0){
  		tmp = i;
  		break;
  	}
  }

  /* Nie znaleziono pliku o zadanej nazwie */
  if(tmp == -1) return 1;

  /* Usuń plik z wirtualnego dysku */
  while(tmp >=0){
    temp = fatTable[tmp];
  	vfs->rootDirectory[tmp].valid = 0;
  	vfs->rootDirectory[tmp].firstBlock = 0;
  	vfs->rootDirectory[tmp].size = 0;
  	vfs->rootDirectory[tmp].create = 0;
  	vfs->rootDirectory[tmp].modify = 0;
	  strcpy(vfs->rootDirectory[tmp].name, "");
    fatTable[tmp] = -2;
    tmp = temp;
  }

  /* Zapisz zmodyfikowaną tablicę alokacji plików (FAT) na dysku */
  free(fatTable);
  closeVFS(vfs);

  printf("File \"%s\" removed successfully from the VFS\n\n", fileName);

  return 0;
}

/* Funkcja viewFiles wyświetla listę plików na wirtualnym dysku.
 * Zwraca 0 w przypadku sukcesu, a 1 w przypadku błędu.
 * Parametr:
 * - name: nazwa wirtualnego dysku */
int viewFiles(char * name){
  unsigned int i;
  struct vfs * vfs;
  char * created;
  char * modified;
  int flag;

  /* Otwórz wirtualny dysk i sprawdź czy jest poprawny*/
  vfs = openVFS(name);
  if(!vfs) return 3;
  flag = 0;

  /* Wyświetl listę plików wraz z informacjami */
  printf("*********** List of files from \"%s\" ***********\n\n", name);
  for(i=0; i<vfs->blockAmount; i++){
  	if(vfs->rootDirectory[i].valid && strcmp(vfs->rootDirectory[i].name, "") != 0){
  		printf("%d. File: %s | Size:%d | Created: %s | Modified: ", i+1, vfs->rootDirectory[i].name, vfs->rootDirectory[i].size, getDate(vfs->rootDirectory[i].create));
      printf("%s | ", getDate(vfs->rootDirectory[i].modify));
      printf("First data block: %d | ", vfs->rootDirectory[i].firstBlock);
      printf("Last data block: %d\n", calculateLastBlock(vfs->rootDirectory[i].firstBlock, vfs->rootDirectory[i].size));
      printf("\n");
      flag = 1;
    }
  }

  if(!flag) printf(" (!) No files on disk (!) \n\n");

  closeVFS(vfs);
  return 0;
}

/* Funkcja viewInfo wyświetla informacje o wirtualnym dysku.
 * Zwraca 0 w przypadku sukcesu, a 1 w przypadku błędu.
 * Parametr:
 * - name: nazwa wirtualnego dysku */
int viewInfo(char * name){
  unsigned int i;
  unsigned int unusedBlocks;
  unsigned int diskSize;
  unsigned int adr;
  struct vfs * vfs;
  struct superblock sb;
  int * fatTable;
  int result;
  unsigned int freeSpace;

  /* Otwórz wirtualny dysk i sprwadź, że jest poprawny*/
  vfs = openVFS(name);
  if(!vfs) return 3;

  /* Przygotuj tablicę alokacji plików (FAT) */
  fatTable = malloc(sizeof(int)*vfs->blockAmount);

  /* Przeczytaj aktualną tablicę alokacji plików (FAT) z wirtualnego dysku */
  fseek(vfs->file, sizeof(struct superblock), 0);
  result = fread(fatTable, sizeof(int), vfs->blockAmount, vfs->file);
  if(!result){
    free(fatTable);
    closeVFS(vfs);
    return 4;
  }

  /* Oblicz rozmiar wirtualnego dysku */
  fseek(vfs->file, 0, 0);
  result = fread(&sb, sizeof(sb), 1, vfs->file);
  if(!result){
    free(fatTable);
    closeVFS(vfs);
    return 5;
  }

  /* Wyświetl nagłówek z informacjami o wirtualnym dysku */
  printf("************ Virtual Disk Info ************\n\n");
  printf("Disk name: %s\n", sb.name);
  printf("Disk size: %d\n", sb.diskSize);
  printf("Number of data blocks: %d (%d B each)\n\n", vfs->blockAmount, sb.blockSize);
  printf("**** Blocks ****\n\n");

  /* Wyświetl informacje o strukturze dysku, tablicy alokacji plików (FAT) i katalogu głównym */
  unusedBlocks = 0;
  printf("------------------------------------------------------------------\n");
  printf("Adr: #0 | Type: SB | Size: %ld\n", sizeof(sb));
  printf("------------------------------------------------------------------\n");
  printf("Adr: #%ld | Type: FAT | Size: %d\n", sizeof(sb), sb.fatBlockSize);
  printf("------------------------------------------------------------------\n");
  printf("Adr: #%ld | Type: rootDirectory | Size: %d\n", sb.fatBlockSize+sizeof(sb), sb.rootdirBlockSize);
  printf("------------------------------------------------------------------\n");

  /* Wyświetl informacje o blokach danych wirtualnego dysku */
  adr = sb.fatBlockSize+sizeof(sb)+sb.rootdirBlockSize;
  for(i=0; i<vfs->blockAmount; i++){

    	printf("Adr: #%d | ", adr+i*BLOCK_SIZE);
    	printf("Type: DataBlock | ");
      freeSpace = vfs->rootDirectory[i].size>sb.blockSize ? 0 : sb.blockSize-vfs->rootDirectory[i].size;
      if (freeSpace == sb.blockSize)
        printf("Usage: Unused | ");
      else
        printf("Usage: Used | ");
    	printf("Free Space: %d/%d\n", freeSpace, sb.blockSize);
      printf("------------------------------------------------------------------\n");

    	if(!(vfs->rootDirectory[i].valid)) unusedBlocks++;
  }

  /* Wyświetl informacje o wolnych blokach danych */
  printf("\n**** Disk usage ****\n\n");
  printf("Free blocks number: %d/%d\n", unusedBlocks, vfs->blockAmount);
  printf("Free space for new file: %d B\n\n", unusedBlocks*BLOCK_SIZE);

  /* Zwolnij pamięć i zamknij pliki */
  free(fatTable);
  closeVFS(vfs);
  return 0;
}

/* Funckja pomocnicza */
int calculateLastBlock(int firstBlock, int size){
  int numberOfBlocks;
  numberOfBlocks = (int)size/BLOCK_SIZE;
  return firstBlock + numberOfBlocks;
}

/* Funckja pomocnicza */
char * getDate(time_t time){
  struct tm * date;
  date = localtime(&time);
  return asctime(date);
}

/* Funkcja pomocnicza */
unsigned int handleFunction(char * name, char * func, char * argv[], int interFlag){
  unsigned int error;

  if (strcmp(func, "-createVFS") == 0 || strcmp(func, "-c") == 0)
  {
    if (interFlag)
    {
      error = createVFS(name, atoi(getUserInput("Enter disk size: ")));
      sleep(2);
    } else {
      error = createVFS(name, atoi(argv[3]));
    }
  }
  else if (strcmp(func, "-copyIntoVD") == 0 || strcmp(func, "-ci") == 0)
  {
    if (interFlag)
    {
      error = copyIntoVD(name, getUserInput("Enter source file name: "), getUserInput("Enter destination file name: "));
      sleep(2);
    } else {
      error = copyIntoVD(name, argv[3], argv[4]);
    }
  }
  else if (strcmp(func, "-copyFromVD") == 0 || strcmp(func, "-cf") == 0)
  {
    if (interFlag)
    {
      error = copyFromVD(name, getUserInput("Enter source file name: "), getUserInput("Enter destination file name: "));
      sleep(2);
    } else {
      error = copyFromVD(name, argv[3], argv[4]);
    }
  }
  else if (strcmp(func, "-removeFromVD") == 0 || strcmp(func, "-rf") == 0)
  {
    if (interFlag)
    {
      error = removeFromVD(name, getUserInput("Enter file name: "));
      sleep(2);
    } else {
      error = removeFromVD(name, argv[3]);
    }
  }
  else if (strcmp(func, "-viewFiles") == 0 || strcmp(func, "-vf") == 0)
  {
    error = viewFiles(name);
    if (interFlag) sleep(2);
  }
  else if (strcmp(func, "-viewInfo") == 0 || strcmp(func, "-vi") == 0)
  {
    error = viewInfo(name);
    if (interFlag) sleep(2);
  }
  else if (strcmp(func, "-removeVFS") == 0 || strcmp(func, "-rv") == 0)
  {
    error = removeVFS(name);
    if (interFlag) sleep(2);
  }
  else if (strcmp(func, "exit") == 0)
  {
    error = 10;
  }
  else
  {
    error = 6;
  }

  return error;
}

/* Funckja pomocnicza */
void printInstructions(char * name) {
  printf("Wrong function name. Usage instructions:\n");
  printf("1. To create a Virtual File System (VFS): %s <VFS_NAME> -createVFS | -c <SIZE>\n", name);
  printf("2. To copy a file from real file system to VFS: %s <VFS_NAME> -copyIntoVD | -ci <SOURCE_FILE_NAME> <DEST_FILE_NAME>\n", name);
  printf("3. To copy a file from VFS to real file system: %s <VFS_NAME> -copyFromVD | -cf <SOURCE_FILE_NAME> <DEST_FILE_NAME>\n", name);
  printf("4. To remove a file from VFS: %s <VFS_NAME> -removeFromVD | -rf <FILE_NAME>\n", name);
  printf("5. To view files on VFS: %s <VFS_NAME> -viewFiles | -vf\n", name);
  printf("6. To view info about VFS: %s <VFS_NAME> -viewInfo | -vi\n", name);
  printf("7. To remove VFS: %s <VFS_NAME> -removeVFS | -rv\n", name);
  printf("8. To enter interactive mode: %s <VFS_NAME> -interactive | -i\n", name);
}

/* Funkcja pomocnicza*/
void handleError(char * name, unsigned int error){
  switch (error)
  {
    case 1:
      printf("Error while handling a file\n");
      break;
    case 2:
      printf("Cannot remove not exsiting VFS\n");
      break;
    case 3:
      printf("Cannot open VFS using given name or the name is empty\n");
      break;
    case 4:
      printf("Error while reading the FAT\n");
      break;
    case 5:
      printf("Not enough free space on VFS\n");
      break;
    case 6:
      printInstructions(name);
      break;
    case 7:
      printf("Disk size is too small\n");
      break;
    default:
      break;
  }
}

/* Funkcja pomocnicza */
char * printUserMenu(void)
{
  int choice;
  char * func;
  
  printf("Interactive Menu:\n");
  printf("1. Create VFS\n");
  printf("2. Copy into Virtual Disk\n");
  printf("3. Copy from Virtual Disk\n");
  printf("4. Remove from Virtual Disk\n");
  printf("5. View Files\n");
  printf("6. View Information\n");
  printf("7. Remove VFS\n");
  printf("0. Exit\n");
  printf("Choose an option: ");

  scanf("%d", &choice);

  switch (choice)
  {
    case 1:
      func = "-createVFS";
      break;
    case 2:
      func = "-copyIntoVD";
      break;
    case 3:
      func = "-copyFromVD";
      break;
    case 4:
      func = "-removeFromVD";
      break;
    case 5:
      func = "-viewFiles";
      break;
    case 6:
      func = "-viewInfo";
      break;
    case 7:
      func = "-removeVFS";
      break;
    case 0:
      func = "exit";
      break;
    default:
      func = "error";
      break;
  }

  return func;
}

/* Funkcja pomocnicza */
char * getUserInput(char * prompt)
{
  char * input;
  input = malloc(sizeof(char) * 64);
  printf("%s", prompt);
  scanf("%s", input);
  return input;
}
