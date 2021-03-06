/*****************************************************
 **
 ** file: doubleBufferedMatrix.c
 **
 ** Copyright (C) 2006-2007    B. M. Bolstad
 **
 ** aim: A class to represent a resizable matrix of doubles.
 **      Where resizable means we may add columns of data.
 **      In addition data is buffered out to storage space.
 **     
 **      Based on ideas and code from my RMAExpress program
 **
 **  History
 **  Jan 31, 2006 - Initial version
 **  Feb 2, 2006 - Fix slight logic error bug in BufferResize
 **  Feb 7, 2006 - functionality for getting a column at a time
 **                either to access it or write to it. Same for
 **                rows. Neither has been written optimally
 **                yet
 **  Feb 16, 2006 - make the internal functions "static", dbm_getPrefix, dbm_getDirectory, dbm_copyValues added
 **  Feb 17, 2006 - added dbm_ewApply for applying C level functions
 **  Feb 22, 2006 - add dbm_max etc. also row*, col* where (*=Mean,Sum,Var,Max,Min)
 **  Jun 28, 2006 - change some loops to memset, more protection in colmode, rowmode switching
 **                 Speed-up optimizations for the getValueCol, getValueCol type functions
 **  Jun 29, 2006 - Add functionality for making better use of cache (ie don't throw something
 **                 out of cache only to have to read it in again, without using it first) for functions
 **                 that work across the whole matrix eg colSums, colMin etc
 **  Jul 17, 2006 - better buffer usage in dbm_getValueRow, dbm_setValueRow, dbm_ewApply
 **  Oct 21, 2006 - add dbm_colMedians
 **  Oct 22, 2006 - add dbm_colRanges, cleaned up some of the NA handling in colSums, colMeans
 **  Oct 27, 2006 - add dbm_getFileName, dbm_fileSpaceInUse, dbm_memoryInUse
 **  Nov 12, 2006 - make fwrite, fread return values get checked. Fix various compiler warnings.
 ** Nov 13, 2006 - optimized colMedians
 ** Jun 16, 2007 -  rename dbm_setDirectory to dbm_setNewDirectory
 ** Sep 9,  2007 - add dbm_rowMedians (only good in rowMode
 **
 *****************************************************/

#include "doubleBufferedMatrix.h"


#include <Rdefines.h>


/*****************************************************
 *****************************************************
 *****************************************************
 **
 ** General Comments
 **
 ** Note all functions here prefixed with "dbm_"
 ** This is an abbreviation for "double buffered matrix"
 **
 ** Basic Idea: Store a Matrix partially in memory.
 **             Partially on disk.
 **             Keep all of this hidden as much as
 **             possible from user.
 **
 ** More general comments:
 **            Assumption is that most access to matrix
 **            is systematic either going down a column
 **            or across a row. Random access will probably be
 **            very, very inefficient.
 **         
 **            two buffers 
 **               - small row buffer - this is the primary buffer when 
 **                          in row mode it contains a contiguous set of rows
 **			     across all columns (ie if you are dealing with adjacent rows
 **                          the code will not have to go to file so often)
 **               - larger column buffer - secondary buffer in row mode
 **                          it contains all the data for a 
 **                          relatively small number of columns
 **
 **            Two modes: column mode (DEFAULT on initialization)
 **                       row mode
 **           
 **            When in column mode the row buffer does not exist.
 **            this means that if the matrix is being accessed across
 **            rows it will be very inefficient. When it is likely that
 **            accessing is going to occur across rows, then a call to:
 **            RowMode() should be called to switch into row mode.
 **            To switch back into column mode use: ColMode()
 **
 **            When switching from column mode to row mode
 **             - allocate space for row buffer
 **             - load data in from files, when data is not 
 **               currently in column buffer
 **             - copy across relevant data that is in column buffer
 **             - set colmode flag to false
 **            
 **            When switching from row mode to column mode
 **            - clear clashes
 **            - flush row buffer (ie write to files)
 **            - deallocate row buffer
 **            - set colmode flag to true
 **
 **
 **            When in "row mode"
 **             the following rules will be used: 
 **           
 **            when data values are being READ from the matrix
 **            - check if it is in small row buffer. If so return value else ...
 **            - check if it is in larger column matrix. If so return value
 **            - fill row buffer with all rows adjacent to row being queried then
 **              remove oldest column from column buffer then put new column into buffer
 **              finally return value
 **
 **            when data values are being written into matrix
 **            - check if location is in small buffer. If it is then
 **              return the address of that location
 **            - If it is not in the row buffer check column buffer.
 **              if it is then return the address of that location
 **            - flush buffers (ie write current row buffer and column buffers
 **              out to file) then refill buffers and return address of cell in row buffer.
 **            
 **            When in "column mode"
 **             the following rules will be used
 **             - check if column is in buffer. If it is return value else ....
 **             - remove oldest column from column buffer then put new column into buffer
 **              finally return value
 **
 **           
 **            Add will work like this:
 **              Create a new temporary file name
 **              Open this temporary file and write # of row zeros
 **              Then increase the rowdata buffer
 **              Remove oldest row data from buffer
 **              Add new column to end of column buffer
 **
 *****************************************************/


/*****************************************************
 *****************************************************
 *****************************************************
 **
 ** The following declares the actual internal stucture
 ** of the doubleBufferedMatrix.
 ** 
 ** Since it is possible that this may be changed in the
 ** future. Direct object manipulation outside this
 ** file is not recommended thus the use of an opaque pointer.
 **
 **
 *****************************************************
 *****************************************************
 *****************************************************/

struct _double_buffered_matrix
{

  int rows;  // number of rows in matrix
  int cols;  // number of cols in matrix
  
  int max_cols; /* Maximum number of cols kept in RAM
                   in column buffered data */

  int max_rows; /* Maximum number of rows kept in RAM
		   in row buffered data  the maximum 
		   value that this should be is 1000 */
  
  double **coldata; /* RAM buffer containing stored data
                       its maximum size should be no more 
                       than max_cols*rows, anything else should be
                       written out temporarily to disk. 

		       If cols is less than max_cols, then entire contents
		       are in RAM
		    */

  double **rowdata; /* RAM buffer containing stored data it size will always
		       be max_rows*cols */

  
  int first_rowdata; /* matrix index of first row stored in rowdata  should be from 0 to rows */

  int *which_cols; /* vector containing indices of columns currently in col data. The 
                      "oldest" indice is first indice. Newest indice is last indice. 
                       Note that the length this will be is min(cols, max_cols) */


  char **filenames; /* contains names of temporary files where data is stored  */

  
  char *fileprefix; /* temporary filenames will begin with this string */
  char *filedirectory; /* path for where directory where temporary files be stored */

  int rowcolclash;  /* referenced a cell location that is both in column and row buffer */
  int clash_row;     /* contains row index of potential clash */
  int clash_col;     /* contains column index of potential clash */
			

  int colmode;      /* If true then in column mode so no rowdata (rows buffer) */
                     /* If false then in row mode, so both column and row buffers */

  int readonly;     /* If true then no need to to carry out "flushing" activities 
		        but the operator [] cannot be used to set values. this should only be 
			set in situations where it is known that only reads are
			going to occur.
			
			If false then flush as normal (this is the default situation) */
  

} _double_buffered_matrix;


/*****************************************************
 *****************************************************
 *****************************************************
 **
 ** The following are declarations for internal
 ** functions
 *****************************************************
 *****************************************************
 *****************************************************/

static void dbm_SetClash(doubleBufferedMatrix Matrix,int row, int col);
static void dbm_ClearClash(doubleBufferedMatrix Matrix);
static int dbm_InRowBuffer(doubleBufferedMatrix Matrix,int row, int col);
static int dbm_InColBuffer(doubleBufferedMatrix Matrix,int row, int col,int *which_col_index);

static int dbm_FlushRowBuffer(doubleBufferedMatrix Matrix);
static int dbm_FlushOldestColumn(doubleBufferedMatrix Matrix);
static int dbm_FlushAllColumns(doubleBufferedMatrix Matrix);

static int dbm_LoadNewColumn(doubleBufferedMatrix Matrix,int col);
static int dbm_LoadRowBuffer(doubleBufferedMatrix Matrix,int row);

static int dbm_LoadAdditionalColumn(doubleBufferedMatrix Matrix,int col, int where);

static double *dbm_internalgetValue(doubleBufferedMatrix Matrix,int row, int col);
static int *dbm_whatsInColumnBuffer(doubleBufferedMatrix Matrix);

/*****************************************************
 *****************************************************
 *****************************************************
 ** 
 ** This next section constitutes  the implementations
 ** of the internal function
 ** 
 ** 
 **
 *****************************************************
 *****************************************************
 *****************************************************/

/*****************************************************
 ** 
 ** void dbm_SetClash(doubleBufferedMatrix Matrix,int row, int col)
 **
 ** doubleBufferedMatrix Matrix - a buffered Matrix object
 ** int row, col - location in matrix for potential clash
 **
 ** This function sets a flag that says whether or not there is
 ** a potential clash between the row buffer and the column buffer
 ** and the location in the matrix of this clash.
 **
 ** A clash means that the values might not agree. When in colmode there
 ** is no chance of a clash because the row buffer does not exist.
 **
 ** in rowmode the rowbuffer is assumed to be the updated one.
 **
 **
 *****************************************************/

static void dbm_SetClash(doubleBufferedMatrix Matrix,int row, int col){
  Matrix->rowcolclash = 1;
  Matrix->clash_row = row;
  Matrix->clash_col = col;
}

/*****************************************************
 ** 
 ** void dbm_ClearClash(doubleBufferedMatrix Matrix)
 **
 ** doubleBufferedMatrix Matrix - a buffered Matrix object
 **
 ** Clears potential clashes by resyncing the column and row buffers.
 **
 *****************************************************/


static void dbm_ClearClash(doubleBufferedMatrix Matrix){

    // Should mean that row buffer is up to date and column buffer is potentially not
  int curcol,lastcol;


  
  curcol = 0;
  if (Matrix->cols < Matrix->max_cols){
    lastcol = Matrix->cols;
  } else {
    lastcol = Matrix->max_cols;
  }
  
  while (curcol < lastcol){
    if (Matrix->which_cols[curcol] == Matrix->clash_col){
      break;
    }
    curcol++;
  }



  if (Matrix->rowdata[Matrix->clash_col][Matrix->clash_row - Matrix->first_rowdata] != Matrix->coldata[curcol][Matrix->clash_row]){
    /* there is a clash, update coldata with current version in rowdata */
    Matrix->coldata[curcol][Matrix->clash_row] = Matrix->rowdata[Matrix->clash_col][Matrix->clash_row - Matrix->first_rowdata];
  } 

  Matrix->rowcolclash=0;
  
}



/*****************************************************
 ** 
 ** int dbm_InRowBuffer(doubleBufferedMatrix Matrix,int row, int col)
 **
 ** doubleBufferedMatrix Matrix
 ** int row, int col - location in Matrix
 **
 ** Returns 1 if the specified element is the Row Buffer
 ** otherwise returns 0 
 **
 *****************************************************/

static int dbm_InRowBuffer(doubleBufferedMatrix Matrix,int row, int col){
  if ((Matrix->first_rowdata <= row) && (row < Matrix->first_rowdata +  Matrix->max_rows)){
    return 1;
  } else {
    return 0;
  }



}

/*****************************************************
 ** 
 ** int dbm_InColBuffer(doubleBufferedMatrix Matrix,int row, int col)
 **
 ** doubleBufferedMatrix Matrix
 ** int row, int col - location in Matrix
 **
 ** Returns 1 if the specified element is the Col Buffer
 ** otherwise returns 0 
 **
 *****************************************************/

static int dbm_InColBuffer(doubleBufferedMatrix Matrix,int row, int col, int *which_col_index){
  int curcol, lastcol;

  if (Matrix->cols < Matrix->max_cols){
    lastcol = Matrix->cols;
  } else {
    lastcol = Matrix->max_cols;
  }
    
    
  curcol = lastcol-1;
  while (curcol >= 0){
    if (Matrix->which_cols[curcol] == col){
      *which_col_index = curcol;
      return 1;  /* Found it */
    }
    curcol--;
  } 
  
  return 0; /* Not found */
}

/*****************************************************
 ** 
 ** int dbm_FlushRowBuffer(doubleBufferedMatrix Matrix)
 **
 ** doubleBufferedMatrix Matrix
 **
 ** Writes entire contents of Row Buffer back out to file
 **
 ** Returns 0 if successful, Returns 1 if problem
 **
 *****************************************************/


static int dbm_FlushRowBuffer(doubleBufferedMatrix Matrix){


  int j;
  
  int blocks_written;

  const char *mode2 ="rb+";
  FILE *myfile;

  for (j =0; j < Matrix->cols; j++){
    myfile = fopen(Matrix->filenames[j],mode2);
    if (myfile == NULL){
      return 1;
    }
    fseek(myfile,Matrix->first_rowdata*sizeof(double),SEEK_SET);
    blocks_written = fwrite(&(Matrix->rowdata)[j][0],sizeof(double),Matrix->max_rows,myfile);
    fclose(myfile);
    if (blocks_written != Matrix->max_rows){
      return 1;
    }
  } 
  return 0;
}

/*****************************************************
 ** 
 ** int dbm_FlushOldestColumn(doubleBufferedMatrix Matrix)
 **
 ** doubleBufferedMatrix Matrix
 **
 ** Writes what is stored in the oldest column (the first  in the
 ** buffer) to file.
 **
 ** Return 1 if problem, 0 if fine.
 **
 *****************************************************/


static int dbm_FlushOldestColumn(doubleBufferedMatrix Matrix){

  int blocks_written;
  
  const char *mode2 ="rb+";
  FILE *myfile;
  
  myfile = fopen(Matrix->filenames[Matrix->which_cols[0]],mode2);
  
  if (myfile == NULL){
    return 1;
  }

  fseek(myfile,0,SEEK_SET); 
  blocks_written = fwrite(Matrix->coldata[0],sizeof(double),Matrix->rows,myfile);
  fclose(myfile);  
  if (blocks_written != Matrix->rows){
      return 1;
  }
  
  return 0;

}


/*****************************************************
 ** 
 ** int dbm_FlushOldestColumn(doubleBufferedMatrix Matrix)
 **
 ** doubleBufferedMatrix Matrix
 **
 ** Writes what is stored in all the columns in the column buffer to
 ** file
 **
 ** Return 1 if problem, 0 if fine.
 **
 **
 ****************************************************/



static int dbm_FlushAllColumns(doubleBufferedMatrix Matrix){



  int k,lastcol;  

  int blocks_written;
  
  const char *mode2 ="rb+";
  FILE *myfile;
 
  if (Matrix->cols < Matrix->max_cols){
    lastcol = Matrix->cols;
  } else {
    lastcol = Matrix->max_cols;
  }
    
  
  for (k=0; k < lastcol; k++){
    myfile = fopen(Matrix->filenames[Matrix->which_cols[k]],mode2);
    if (myfile == NULL){
      return 1;
    }
    fseek(myfile,0,SEEK_SET); 
    blocks_written = fwrite(Matrix->coldata[k],sizeof(double),Matrix->rows,myfile);
    fclose(myfile);  
    if (blocks_written != Matrix->rows){
      return 1;
    }
 
  }

  return 0;

}


/*****************************************************
 ** 
 ** void dbm_LoadNewColumn(doubleBufferedMatrix Matrix,int col);
 **
 ** doubleBufferedMatrix Matrix
 ** int col - column of the matrix to load into the buffer
 **
 ** Read the specified column into the column buffer (at the end of the buffer)
 **
 ** Works by moving the oldest column from the  column buffer (at the beginning of the buffer)
 ** to the newest (at end) and then overwriting by reading in new data from file 
 **
 ** Returns 0 if successful, returns 1 if problem
 **
 ****************************************************/

static int dbm_LoadNewColumn(doubleBufferedMatrix Matrix,int col){
  
  const char *mode = "rb";
  FILE *myfile;
  double *tmpptr;
  int lastcol;
  int j;
  int blocks_read;

  if (Matrix->cols < Matrix->max_cols){
    lastcol = Matrix->cols;
  } else {
    lastcol = Matrix->max_cols;
  }
  
  tmpptr = Matrix->coldata[0];

  for (j=1; j < lastcol; j++){
    Matrix->coldata[j-1] = Matrix->coldata[j];
    Matrix->which_cols[j-1] = Matrix->which_cols[j];
  }
  
  Matrix->which_cols[lastcol -1] = col;
  Matrix->coldata[lastcol -1] = tmpptr;
  
  //printf("loading column %d \n",whichcol);
  myfile = fopen(Matrix->filenames[col],mode);
  if (myfile == NULL){
    return 1;
  }
  fseek(myfile,0,SEEK_SET);
  blocks_read = fread(Matrix->coldata[lastcol -1],sizeof(double),Matrix->rows,myfile);
  fclose(myfile);

  if (blocks_read != Matrix->rows){
    return 1;
  }


  return 0;
  
}

/*****************************************************
 ** 
 ** void dbm_LoadNewColumn_nofill(doubleBufferedMatrix Matrix,int col);
 **
 ** doubleBufferedMatrix Matrix
 ** int col - column of the matrix to load into the buffer
 **
 ** Read the specified column into the column buffer (at the end of the buffer)
 **
 ** Works by moving the oldest column from the  column buffer (at the beginning of the buffer)
 ** to the newest (at end). Does not fill it with new data. The calling function should appropriately
 ** handle this.
 **
 ** WARNING: If you don't no why this function exists you should probably
 **          be calling dbm_LoadNewColumn instead
 **
 ** Returns 0 if successful, returns 1 if problem
 **
 ****************************************************/

static int dbm_LoadNewColumn_nofill(doubleBufferedMatrix Matrix,int col){
  
  double *tmpptr;
  int lastcol;
  int j;


  if (Matrix->cols < Matrix->max_cols){
    lastcol = Matrix->cols;
  } else {
    lastcol = Matrix->max_cols;
  }
  
  tmpptr = Matrix->coldata[0];

  for (j=1; j < lastcol; j++){
    Matrix->coldata[j-1] = Matrix->coldata[j];
    Matrix->which_cols[j-1] = Matrix->which_cols[j];
  }
  
  Matrix->which_cols[lastcol -1] = col;
  Matrix->coldata[lastcol -1] = tmpptr;
  
  //printf("loading column %d \n",whichcol);
  return 0;
  
}



/*****************************************************
 ** 
 ** int dbm_LoadRowBuffer(doubleBufferedMatrix Matrix,int row)
 **
 ** doubleBufferedMatrix Matrix
 ** int row  - row to read into row buffer
 **
 ** Reads the specified row and adjacent rows into the 
 ** row buffer.
 ** 
 ** Returns 0 if successful, returns 1 if problem
 **
 *****************************************************/




static int dbm_LoadRowBuffer(doubleBufferedMatrix Matrix,int row){


  const char *mode = "rb";
  FILE *myfile;
  int j,k;
  int lastcol;
  int curcol;
  int blocks_read;



  if (Matrix->cols < Matrix->max_cols){
    lastcol = Matrix->cols;
  } else {
    lastcol = Matrix->max_cols;
  }
  
  if (row > Matrix->rows - Matrix->max_rows){
    Matrix->first_rowdata = Matrix->rows - Matrix->max_rows;
  } else {
    Matrix->first_rowdata = row;
  }
    
  for (j =0; j < Matrix->cols; j++){
    
    myfile = fopen(Matrix->filenames[j],mode);

    if (myfile == NULL)
      return 1;


    fseek(myfile,Matrix->first_rowdata*sizeof(double),SEEK_SET);
    blocks_read = fread(&(Matrix->rowdata)[j][0],sizeof(double),Matrix->max_rows,myfile);

    fclose(myfile);  

    if (blocks_read != Matrix->max_rows){
      return 1;
    }

  }
  
  for (j =0; j < Matrix->cols; j++){
    curcol =0;
    while (curcol < lastcol){
      if (Matrix->which_cols[curcol] == j){
	for (k= Matrix->first_rowdata; k < Matrix->first_rowdata + Matrix->max_rows; k++){
	  Matrix->rowdata[j][k- Matrix->first_rowdata] = Matrix->coldata[curcol][k];
	}	
      }
      curcol++;
    }
  }

  return 0;

}






/*****************************************************
 ** 
 ** int dbm_LoadAdditionalColumn(doubleBufferedMatrix Matrix,int col, int where)
 **
 ** doubleBufferedMatrix Matrix
 ** int col - column to read into buffer
 ** int where - which column of the buffer do we read it into.
 **
 ** Allocates space and reads in a new column of data.
 ** 
 ** Returns 0 if successful, returns 1 if problem
 **
 *****************************************************/


static int dbm_LoadAdditionalColumn(doubleBufferedMatrix Matrix,int col, int where){
  const char *mode = "rb";
  FILE *myfile;
  int blocks_read;

  Matrix->coldata[where] = Calloc(Matrix->rows,double);
  Matrix->which_cols[where] = col;
  myfile = fopen(Matrix->filenames[col],mode);
  if (myfile == NULL)
    return 1;
  fseek(myfile,0,SEEK_SET);
  blocks_read = fread(Matrix->coldata[where],sizeof(double),Matrix->rows,myfile);
  fclose(myfile);

  if (blocks_read != Matrix->rows)
    return 1;


  return 0;
}

/*****************************************************
 ** 
 ** double *dbm_internalgetValue(doubleBufferedMatrix Matrix,int row, int col)
 **
 **
 ** this function returns a pointer to a location containing current value
 ** of element located at (row,col) in the matrix. Carries out all the necessary
 ** mechanics of loading it into the buffer if it is not there currently.
 **
 *****************************************************/

static double *dbm_internalgetValue(doubleBufferedMatrix Matrix,int row, int col){
  
  
  int whichcol = col;
  int whichrow = row;
 
  int curcol;
 

  if (!(Matrix->colmode)){
    /* Fix up any potential clashes */
    if ((Matrix->rowcolclash)){
      dbm_ClearClash(Matrix);
    }
    
    /* check to see if this cell is in row buffer, then column buffer, then read from files */
    
    if (dbm_InRowBuffer(Matrix,whichrow,whichcol)){
      /* In row buffer so return */
      if (dbm_InColBuffer(Matrix,whichrow,whichcol,&curcol)){
	dbm_SetClash(Matrix, whichrow,whichcol);
      }
      
      return &(Matrix->rowdata[whichcol][whichrow - Matrix->first_rowdata]);
    } else if (dbm_InColBuffer(Matrix,whichrow,whichcol,&curcol)){
      return &(Matrix->coldata[curcol][whichrow]);
    } else {
      
      /* looks like we are going to have to go to files */
      //printf("Couldn't find in buffers\n");

      if (!(Matrix->readonly)){
	/* Flush buffers */ 
	/* First row buffer */
	dbm_FlushRowBuffer(Matrix);
      
	/* Now flush the column buffer (for oldest column) */
	dbm_FlushOldestColumn(Matrix);
      }
      
      /* Now fill up the buffer */
      /* read in this row and surrounding rows into row buffer */
      dbm_LoadRowBuffer(Matrix,whichrow);
      
      /* read in this column into column buffer */
      dbm_LoadNewColumn(Matrix,whichcol);
      
      
      dbm_SetClash(Matrix,whichrow,whichcol);
      return &(Matrix->rowdata[whichcol][whichrow - Matrix->first_rowdata]);
    
    }
  } else {
    if (dbm_InColBuffer(Matrix,whichrow,whichcol,&curcol)){
      return &(Matrix->coldata[curcol][whichrow]);
    } else {
      if (!(Matrix->readonly))
	dbm_FlushOldestColumn(Matrix); 
      dbm_LoadNewColumn(Matrix,whichcol);
      return &(Matrix->coldata[Matrix->max_cols -1][whichrow]);
    }

  }

   
}


/*****************************************************
 ** 
 ** static int *dbm_whatsInColumnBuffer(doubleBufferedMatrix Matrix)
 **
 ** Returns a pointer to a vector containing column indices 
 **
 **
 *****************************************************/


static int *dbm_whatsInColumnBuffer(doubleBufferedMatrix Matrix){

  return Matrix->which_cols;
  
}






/*****************************************************
 *****************************************************
 *****************************************************
 **
 ** The following are functions that should be considered
 ** exposed to the real world. Ie the constitute the 
 ** API that might be used by others.
 ** Every thing above this point is internal, subject to
 ** be changed and not to be called except from within
 ** the routines below.
 ** 
 **
 **
 **
 *****************************************************
 *****************************************************
 *****************************************************/

/*****************************************************
 **
 ** doubleBufferedMatrix dbm_alloc(int max_rows,int max_cols,const char *prefix, const char *directory)
 **
 ** int max_rows - maximum number of rows that can be stored in the row buffer (if it is activated)
 ** int max_cols - maximum number of columns that can be stored in the column buffer
 ** const char *prefix - character string to use for start of filename for temporary files. 
 ** const char *directory - character string for 
 **
 ** 
 ** RETURNS an allocated empty doubleBufferedMatrix. Note that this routine
 **         does not actually allocate the space for the data that is stored.
 **         This is done by the AddColumn routine.
 **                               
 **
 *****************************************************/


doubleBufferedMatrix dbm_alloc(int max_rows,int max_cols,const char *prefix, const char *directory){
  
  char *tmp;

  struct _double_buffered_matrix *handle;

  
  handle = (struct _double_buffered_matrix *)Calloc(1,struct _double_buffered_matrix);




  handle->rows =0;
  handle->cols =0;
  handle->max_rows = max_rows;
  handle->max_cols = max_cols;
  
  handle->coldata = 0;
  handle->rowdata = 0;
  
  handle->which_cols = 0;

  handle->filenames = 0;
  
  handle->first_rowdata =0;

  tmp = Calloc(strlen(prefix)+1,char);
  strcpy(tmp,prefix);

  handle->fileprefix = tmp;
  
  tmp = Calloc(strlen(directory)+1,char);
  strcpy(tmp,directory);
  
  handle->filedirectory = tmp;

  handle->rowcolclash = 0;

  handle->colmode = 1;        /* Always start of in column mode */

  handle->readonly=0;
  
  return (doubleBufferedMatrix)handle;

}

/*****************************************************
 **
 ** int dbm_free(doubleBufferedMatrix Matrix)
 ** 
 ** doubleBufferedMatrix *Matrix 
 ** 
 ** Deallocates all allocated space and deletes temporary files
 **
 *****************************************************/

int dbm_free(doubleBufferedMatrix Matrix){
  
  int i;
  int lastcol;
  struct _double_buffered_matrix *handle;
  
  handle = Matrix;

  if (handle->cols < handle->max_cols){
    lastcol = handle->cols;
  } else {
    lastcol = handle->max_cols;
  }

  for (i=0; i < handle->cols; i++){
    //printf("%s\n",filenames[i]);
    remove(handle->filenames[i]);
  }

  Free(handle->which_cols);

  for (i = 0; i < handle->cols; i++){
    Free(handle->filenames[i]);
  }
  Free(handle->filenames);

  if (!(handle->colmode)){
    for (i=0; i < handle->cols; i++){
      Free(handle->rowdata[i]);
    }
    Free(handle->rowdata);
  }


  for (i=0; i < lastcol; i++){
    Free(handle->coldata[i]);
  }
  Free(handle->coldata);
  

  Free(handle->fileprefix);
  Free(handle->filedirectory);

  Free(handle);
  return 0;
}


/*****************************************************
 **
 ** int dbm_setRows(doubleBufferedMatrix Matrix, int Rows)
 **
 ** doubleBufferedMatrix Matrix
 ** int Rows - number of rows in each column of the matrix
 ** 
 ** returns 1 if successful, 0 otherwise
 **
 ** Note that this function sets the number of rows in
 ** the matrix. Once set the number of rows can not be altered.
 **
 *****************************************************/


int dbm_setRows(doubleBufferedMatrix Matrix, int Rows){


  if (Matrix->rows > 0){
    return 0;
  }

  Matrix->rows = Rows;

  if (Matrix->rows < Matrix->max_rows){
    Matrix->max_rows = Matrix->rows;
  }

  
  return 1;

}

/*****************************************************
 **
 ** int dbm_AddColumn(doubleBufferedMatrix Matrix)
 ** 
 ** doubleBufferedMatrix Matrix
 **
 ** Adds an additional column to the matrix at edge of 
 ** Matrix. Note this entails creating an additional 
 ** temporary file. Note also that the number of rows 
 ** in the matrix should have already been set by 
 ** Previously calling dbm_setRows().
 **
 ** RETURNS 0 is successful and 1 if problem
 **
 *****************************************************/


int dbm_AddColumn(doubleBufferedMatrix Matrix){

  
  FILE *myfile;
  int j;
  int which_col_num;

  int blocks_written;
  
  /* Handle the housekeeping of indices, clearing buffer if needed etc */
  if (Matrix->cols < Matrix->max_cols){
    /* No need to clear out column buffer */
    int *temp_indices = Calloc(Matrix->cols+1, int);
    int *temp_old_indices = Matrix->which_cols;
    double **temp_ptr = Calloc(Matrix->cols +1,double *);
    double **old_temp_ptr = Matrix->coldata;

    for (j =0; j < Matrix->cols; j++){
      temp_indices[j] = Matrix->which_cols[j];
      temp_ptr[j] = Matrix->coldata[j];
    }
    temp_indices[Matrix->cols] =Matrix->cols;
    temp_ptr[Matrix->cols] = Calloc(Matrix->rows,double);

    Matrix->coldata = temp_ptr;
    
    /* for (i =0; i < Matrix->rows; i++){
       Matrix->coldata[Matrix->cols][i] = 0.0;  //(cols)*rows + i; 
       } */
    memset(&Matrix->coldata[Matrix->cols][0],0,sizeof(double)* Matrix->rows);



    which_col_num = Matrix->cols;
    Matrix->which_cols = temp_indices;
    Free(temp_old_indices);
    Free(old_temp_ptr);

    if (!(Matrix->colmode)){
      /* Now handle the row buffer */
      old_temp_ptr = Matrix->rowdata;
      temp_ptr = Calloc(Matrix->cols+1,double *);
      
      for (j =0; j <  Matrix->cols; j++){
	temp_ptr[j] =  Matrix->rowdata[j];
      }
      temp_ptr[Matrix->cols] = Calloc(Matrix->max_rows,double);
      
      /* for (i=0; i < Matrix->max_rows; i++){
	 temp_ptr[Matrix->cols][i] = 0.0;   // (cols)*rows + i; 
	 }
      */
      memset(&temp_ptr[Matrix->cols][0],0,sizeof(double)* Matrix->max_rows);



      Matrix->rowdata = temp_ptr;
      Free(old_temp_ptr);
    }

  } else {
    /* Need to remove oldest column from buffer */
    double **temp_ptr;
    double *temp_col = Matrix->coldata[0];
    double **old_temp_ptr = Matrix->rowdata;
 
    /* Before we deallocate, better empty the column buffer */
    myfile = fopen(Matrix->filenames[Matrix->which_cols[0]],"rb+");
    blocks_written = fwrite(&temp_col[0],sizeof(double),Matrix->rows,myfile);
    fclose(myfile); 
    
    if (blocks_written != Matrix->rows){
      return 1;
    }

    
    for (j =1; j < Matrix->max_cols; j++){
      Matrix->which_cols[j-1] = Matrix->which_cols[j];
      Matrix->coldata[j-1] = Matrix->coldata[j];
    }
    Matrix->which_cols[Matrix->max_cols-1] = Matrix->cols;
    Matrix->coldata[Matrix->max_cols-1] = temp_col; //new double[this->rows];
    /* 
       for (i =0; i < Matrix->rows; i++){
       Matrix->coldata[Matrix->max_cols-1][i] = 0.0; // (cols)*rows +i;
       }
    */
    memset(&Matrix->coldata[Matrix->max_cols-1][0],0,sizeof(double)* Matrix->rows);

    
    which_col_num = Matrix->max_cols-1;

   
    
    if (!(Matrix->colmode)){
      old_temp_ptr = Matrix->rowdata;
      temp_ptr = Calloc(Matrix->cols+1,double *);
      
      for (j =0; j < Matrix->cols; j++){
	temp_ptr[j] = Matrix->rowdata[j];
      }
      temp_ptr[Matrix->cols] = Calloc(Matrix->max_rows,double);
      
      /*
	for (i=0; i < Matrix->max_rows; i++){
	temp_ptr[Matrix->cols][i] = 0.0;      //(cols)*rows + i;
	}
      */
      memset(&temp_ptr[Matrix->cols][0],0,sizeof(double)* Matrix->max_rows);
      
      Matrix->rowdata = temp_ptr;
      Free(old_temp_ptr);
    }


  }
  /* now do the file stuff */

  char **temp_filenames = Calloc(Matrix->cols+1,char *);
  char *temp_name;
  char **temp_names_ptr = Matrix->filenames;


  for (j =0; j < Matrix->cols; j++){
    temp_filenames[j] = Matrix->filenames[j];
  }

 

  temp_name = (char *)R_tmpnam(Matrix->fileprefix,Matrix->filedirectory);

  char *tmp = Calloc(strlen(temp_name)+1,char);
  strcpy(tmp,temp_name);

  temp_filenames[Matrix->cols] = Calloc(strlen(tmp)+1,char);
  temp_filenames[Matrix->cols] = strcpy(temp_filenames[Matrix->cols],tmp);

  Matrix->filenames = temp_filenames;

  /*   SHOULD NEVER HAVE BEEN HERE. CAUSED CRASHES ON WINDOWS Free(temp_name); */
  Free(temp_names_ptr);
  Free(tmp);


  /* Finally lets write it all out to a file */

  const char *mode = "wb";
  //printf("%s\n", filenames[cols]);
  myfile = fopen(temp_filenames[Matrix->cols],mode);
  if (!myfile){
    return 1;            /** Bad error **/
  }
  blocks_written = fwrite(Matrix->coldata[which_col_num],sizeof(double),  Matrix->rows, myfile);

  if (blocks_written != Matrix->rows){
    return 1;
  }

  fclose(myfile);
  Matrix->cols++;

  return 0;
  
}

/*****************************************************
 **
 ** int dbm_ResizeColBuffer(doubleBufferedMatrix Matrix, int new_maxcol)
 **
 ** doubleBufferedMatrix Matrix
 ** int new_maxcol - the number of columns that should be stored in the column buffer
 **                  upon exit from this routine.
 **
 ** This function deals with the mechanics of loading new data in or emptying
 ** data out of the the column buffer depending on whether the size of the
 ** column buffer is to be increased or decreased.
 ** 
 **
 *****************************************************/

int dbm_ResizeColBuffer(doubleBufferedMatrix Matrix, int new_maxcol){


  int i,j;
  int lastcol;
  int n_cols_remove=0;
  int n_cols_add=0; 
  double *tmpptr;
  double **tmpptr2;
  int *tmpptr3;

  int *whichadd;

  int curcol;
  int min_j;


    /* Fix up any potential clashes */
  if (Matrix->rowcolclash){
    dbm_ClearClash(Matrix);
  }
  // First figure out if need to mak any changes

  if (new_maxcol <= 0){
    return 1;  /** Big big error **/
  }

  if (Matrix->cols < Matrix->max_cols){
    lastcol = Matrix->cols;
  } else {
    lastcol = Matrix->max_cols;
  }
  

  if (Matrix->max_cols == new_maxcol){
    // No need to do anything.
    return 0;
  } else if (Matrix->max_cols > new_maxcol){
    // Remove columns from the column buffer
    // Will remove max_col - new_maxcol oldest columns
    if (new_maxcol < Matrix->cols){
      if (Matrix->max_cols < Matrix->cols){
	n_cols_remove = Matrix->max_cols - new_maxcol;
      } else{
	n_cols_remove = Matrix->cols - new_maxcol;
      }


      for (i=0; i < n_cols_remove; i++){
	dbm_FlushOldestColumn(Matrix);
	tmpptr = Matrix->coldata[0];
	for (j=1; j < lastcol; j++){
	  Matrix->coldata[j-1] = Matrix->coldata[j];
	  Matrix->which_cols[j-1] = Matrix->which_cols[j];
	}
	Free(tmpptr);
      }
      
      tmpptr2 = Matrix->coldata;
      tmpptr3 = Matrix->which_cols;
      
      Matrix->coldata = Calloc(new_maxcol,double *);
      Matrix->which_cols = Calloc(new_maxcol,int);
      
      for (j=0; j < new_maxcol; j++){
	Matrix->coldata[j] = tmpptr2[j];
	Matrix->which_cols[j] = tmpptr3[j];
      }
      Free(tmpptr2);
      Free(tmpptr3);
    }
    Matrix->max_cols = new_maxcol;

  } else {
    // Need to add columns to the column buffer
    
    if (new_maxcol < Matrix->cols){
      n_cols_add = new_maxcol - Matrix->max_cols;
    } else if (Matrix->max_cols < Matrix->cols){
      n_cols_add = Matrix->cols - Matrix->max_cols;
    } else {
      // there are no more columns to add, everything is already in the buffer
      n_cols_add = 0;
      Matrix->max_cols = new_maxcol;
      return 0;
    }
    
    // Figure out which columns to add
    // rule will be to add columns in numerical order (ie column 0 if not it, then 1 if not in and so on)
    
    whichadd = Calloc(n_cols_add,int);
    
    min_j=0;
    for (i=0; i < n_cols_add; i++){
      for (j=min_j; j < Matrix->cols; j++){ /************************** *****/
	//	printf("j is %d %d ",j, min_j);
	if(!dbm_InColBuffer(Matrix,0,j,&curcol)){
	  whichadd[i] = j;
	  //printf("Adding %d\n",j);
	  break;
	}
      }
      min_j = j+1;
    }
    
    // Add columns to end of buffer
    tmpptr2 = Matrix->coldata;
    tmpptr3 = Matrix->which_cols;
    
    Matrix->coldata = Calloc(Matrix->max_cols+ n_cols_add, double *);
    Matrix->which_cols = Calloc(new_maxcol+ n_cols_add,int);  
    for (j=0; j < Matrix->max_cols; j++){
      Matrix->coldata[j] = tmpptr2[j];
      Matrix->which_cols[j] = tmpptr3[j];
    }
    
    for (i=0; i < n_cols_add; i++){
      dbm_LoadAdditionalColumn(Matrix,whichadd[i], Matrix->max_cols + i);
    }
    Free(tmpptr2);
    Free(tmpptr3);
    Free(whichadd);

    Matrix->max_cols = new_maxcol;
  }

  return 0;

}

/*****************************************************
 **
 ** int dbm_ResizeRowBuffer(doubleBufferedMatrix Matrix, int new_maxrow)
 **
 ** doubleBufferedMatrix Matrix
 ** int new_maxrow - the number of rows that should be stored in the row buffer
 **                  upon exit from this routine (or at least the number possible
 **                  if thw row buffer was active, if in column mode)
 **
 **
 ** Returns 0 if successful, 1 if problem.
 **
 *****************************************************/



int dbm_ResizeRowBuffer(doubleBufferedMatrix Matrix, int new_maxrow){


  int i, j;
  /* 
     int n_rows_remove=0;
     int n_rows_add=0;
  */
  double *tmpptr;
  int new_first_rowdata;

  if (new_maxrow <= 0){
    return 1;  /** big error **/


  }

  if (new_maxrow > Matrix->rows){
    new_maxrow = Matrix->rows;
  }

  if (Matrix->colmode){
    Matrix->max_rows =new_maxrow;
    return 0;
  }


  /* Fix up any potential clashes */
  if (Matrix->rowcolclash){
    dbm_ClearClash(Matrix);
  }

  if (Matrix->max_rows == new_maxrow){
    // No need to do anything.
    return 0;
  } else if (Matrix->max_rows > new_maxrow){
    // Remove rows from the rows buffer
    // Empty out row buffer (at least resync with files)
    dbm_FlushRowBuffer(Matrix);
    
    for (j =0; j < Matrix->cols; j++){
      // printf("fixing col %d in row buffer\n",j);
      tmpptr = Matrix->rowdata[j];
      Matrix->rowdata[j] = Calloc(new_maxrow,double);
      for (i=0; i < new_maxrow; i++){
	 Matrix->rowdata[j][i] = tmpptr[i];
      }
      Free(tmpptr);
    }
    Matrix->max_rows = new_maxrow;
  } else {
    // Increase number of rows in row buffer
    
    // Empty out row buffer (at least resync with files)
    dbm_FlushRowBuffer(Matrix);

    // Allocate Spaceint new_maxcol
    
    for (j =0; j < Matrix->cols; j++){ 
      tmpptr = Matrix->rowdata[j];
      Matrix->rowdata[j] = Calloc(new_maxrow,double);
      Free(tmpptr);
    }
      

    // Now see if we will be hitting the bottom of the matrix with the added rows
    
    if (Matrix->first_rowdata + new_maxrow > Matrix->rows){
      new_first_rowdata = Matrix->rows - new_maxrow;
    } else {
      new_first_rowdata = Matrix->rows;
    }
    Matrix->max_rows = new_maxrow;
    dbm_LoadRowBuffer(Matrix,new_first_rowdata);
    
  }

  return 0;
}



/*****************************************************
 **
 ** int dbm_ResizeBuffer(doubleBufferedMatrix Matrix, int new_maxrow,int new_maxcol)
 **
 ** doubleBufferedMatrix Matrix
 ** int new_maxrow - the number of rows that should be stored in the row buffer
 ** int new_maxrow - the number of rows that should be stored in the row buffer
 **                  upon exit from this routine (or at least the number possible
 **                  if thw row buffer was active, if in column mode)
 **
 **
 **
 **
 *****************************************************/

int dbm_ResizeBuffer(doubleBufferedMatrix Matrix, int new_maxrow, int new_maxcol){

  dbm_ResizeColBuffer(Matrix,new_maxcol);
  if (!(Matrix->colmode)){
    dbm_ResizeRowBuffer(Matrix,new_maxrow);
  } else {
    /* No actual row buffer active. So just increase potential size.
       with caveats: Can't: Be smaller than 1 or be bigger than number of rows
       if those cases exist go to nearest possible value. ie 1 or max

    */
    if (new_maxrow < 1){
      Matrix->max_rows = 1;
    } else if (new_maxrow > Matrix->rows){
      Matrix->max_rows = Matrix->rows;
    } else {
      Matrix->max_rows = new_maxrow;
    } 
  }
  return 0;
}


/******************************************************
 **
 ** void dbm_RowMode(doubleBufferedMatrix Matrix)
 **
 ** Switch to Row Mode 
 **
 ******************************************************/



void dbm_RowMode(doubleBufferedMatrix Matrix){
  int j;

  /**            When switching from column mode to row mode
   **             - allocate space for row buffer
   **             - load data in from files, when data is not 
   **               currently in column buffer
   **             - copy across relevant data that is in column buffer
   **             - set colmode flag to false
   */
  if (Matrix->colmode == 1){
    Matrix->rowdata = Calloc(Matrix->cols +1,double *);
    for (j =0; j < Matrix->cols; j++){
      Matrix->rowdata[j] = Calloc(Matrix->max_rows,double);
    }
    dbm_LoadRowBuffer(Matrix,0); /* this both fills the row buffer and copys across anything in the current column buffer */
    Matrix->colmode =0;
  }
  
}


/******************************************************
 **
 ** void dbm_ColMode(doubleBufferedMatrix Matrix)
 **
 ** Switch to column Mode (ie turns off row mode)
 **
 ******************************************************/

void dbm_ColMode(doubleBufferedMatrix Matrix){

  int j;
  /* **            When switching from row mode to column mode
  **            - clear clashes
  **            - flush row buffer (ie write to files)
  **            - deallocate row buffer
  **            - set colmode flag to true
  ** */
  if (Matrix->colmode == 0){
    if (Matrix->rowcolclash){
      dbm_ClearClash(Matrix);
    }
    dbm_FlushRowBuffer(Matrix);
    
    for (j =0; j < Matrix->cols; j++){
      Free(Matrix->rowdata[j]);
    }
    Free(Matrix->rowdata);
    Matrix->colmode = 1;
  }

}

/******************************************************
 **
 ** void dbm_SetPrefix(doubleBufferedMatrix Matrix,const char *prefix)
 **
 ** Sets initial part of the filename of temporary files
 ** used for storing matrix.
 **
 ******************************************************/



void dbm_SetPrefix(doubleBufferedMatrix Matrix,const char *prefix){

  char *tmp;

  tmp = Calloc(strlen(prefix)+1,char);
  strcpy(tmp,prefix);
  
  if (Matrix->fileprefix != NULL){
    Free(Matrix->fileprefix);
  }
  Matrix->fileprefix = tmp;

}


/******************************************************
 **
 ** void dbm_ReadOnlyMode(doubleBufferedMatrix Matrix, int setting)
 **
 ** 
 **
 ** Set ReadOnlyMode on or off.
 **
 ******************************************************/

void dbm_ReadOnlyMode(doubleBufferedMatrix Matrix, int setting){


  /* 
     If readonly mode is true we don't need to do anything
     extra except set the flag.

     If however readonly is false and we are changing to
     true then we will need to flush the current
     buffers to ensure coherency.

  */


  if (!(Matrix->readonly) & setting){
    if (!(Matrix->colmode)){
      if (Matrix->rowcolclash){
	dbm_ClearClash(Matrix);
      }
      dbm_FlushRowBuffer(Matrix);
    }
    dbm_FlushAllColumns(Matrix);
  } 



  Matrix->readonly = setting;

}

/******************************************************
 **
 ** int dbm_isReadOnlyMode(doubleBufferedMatrix Matrix)
 **
 ** doubleBufferedMatrix Matrix
 **
 ** returns 1 if read only mode is activated otherwise returns 0
 **
 ******************************************************/


int dbm_isReadOnlyMode(doubleBufferedMatrix Matrix){

  return (Matrix->readonly);
}


/******************************************************
 **
 ** int dbm_isRowMode(doubleBufferedMatrix Matrix)
 **
 ** doubleBufferedMatrix Matrix
 **
 ** returns 1 if row buffer is activated otherwise returns 0
 **
 ******************************************************/


int dbm_isRowMode(doubleBufferedMatrix Matrix){

  return (!(Matrix->colmode));
}


/******************************************************
 **
 ** int dbm_getValue(doubleBufferedMatrix Matrix, int row, int col, double *value)
 **
 ** doubleBufferedMatrix Matrix
 ** int row, col - location in matrix
 ** double *value - location to store value found in matrix
 **
 ** Returns 1 if get was successful. Otherwise returns 0 and *value is unchanged.
 **
 **
 ******************************************************/

int dbm_getValue(doubleBufferedMatrix Matrix, int row, int col, double *value){

  double *tmp;

  if ((row >= Matrix->rows) || (col >= Matrix->cols) || (row < 0) || (col < 0)){
    return 0;
  }


  tmp = dbm_internalgetValue(Matrix,row,col);
  
  *value = *tmp;
  
  if (!Matrix->colmode && Matrix->readonly){
    Matrix->rowcolclash = 0;  /* If readonly. No need to worry about clashes */
  }


  return 1;
}


/******************************************************
 **
 ** int dbm_setValue(doubleBufferedMatrix Matrix, int row, int col, double value)
 **
 ** doubleBufferedMatrix Matrix
 ** int row, col - location in matrix
 ** double value - value to store in matrix
 **
 **
 ******************************************************/

int dbm_setValue(doubleBufferedMatrix Matrix, int row, int col, double value){
  double *tmp;
  
  if (Matrix->readonly){
    return 0; /*Not successful */

  } else {
    if ((row >= Matrix->rows) || (col >= Matrix->cols) || (row < 0) || (col < 0)){
      return 0;
    }
    
    tmp = dbm_internalgetValue(Matrix,row,col);
    *tmp = value;
    return 1; /*Successful */
  }

}

/******************************************************
 **
 ** int dbm_getValueSI(doubleBufferedMatrix Matrix, int index, double *value)
 **
 ** doubleBufferedMatrix Matrix
 ** int index - location in matrix
 ** double *value - location to store value found in matrix
 **
 ** Uses single indexing rather than dual indexing to locate a
 ** value in the matrix. copys this value into location of supplied
 ** variable.
 **
 ** Returns 1 if get was successful. Otherwise returns 0 and *value is unchanged.
 **
 ******************************************************/



int dbm_getValueSI(doubleBufferedMatrix Matrix, int index, double *value){
  double *tmp;
  int whichcol = index/Matrix->rows;
  int whichrow = index % Matrix->rows;

  if ((whichcol >= Matrix->cols) || (whichrow >= Matrix->rows) || (whichrow < 0) || (whichcol < 0)){
    return 0;
  }
  
  tmp = dbm_internalgetValue(Matrix,whichrow,whichcol);
  
  *value = *tmp;

  if (!Matrix->colmode && Matrix->readonly){
    Matrix->rowcolclash = 0;  /* If readonly. No need to worry about clashes */
  }
  
  return 1;

}

/******************************************************
 **
 ** int dbm_setValueSI(doubleBufferedMatrix Matrix, int index, double value)
 **
 ** doubleBufferedMatrix Matrix
 ** int index - location in matrix
 ** double *value - location to store value found in matrix
 **
 ** Uses single indexing rather than dual indexing to locate a
 ** value in the matrix. Sets value. 
 **
 ******************************************************/



int dbm_setValueSI(doubleBufferedMatrix Matrix, int index, double value){
  double *tmp;
  int whichcol = index/Matrix->rows;
  int whichrow = index % Matrix->rows;
  
  if (Matrix->readonly){
    return 0; /* not successful */

  } else {  

    if ((whichcol >= Matrix->cols) || (whichrow >= Matrix->rows) || (whichrow < 0) || (whichcol < 0)){
      return 0;
    }
  
    tmp = dbm_internalgetValue(Matrix,whichrow,whichcol);
  
    *tmp = value;
    return 1; /* successful */
  }
}


/******************************************************
 **
 ** int dbm_getRowss(doubleBufferedMatrix Matrix)
 **
 ** returns the number of rows in the matrix 
 **
 **
 ******************************************************/


int dbm_getRows(doubleBufferedMatrix Matrix){
  return(Matrix->rows);
}


/******************************************************
 **
 ** int dbm_getCols(doubleBufferedMatrix Matrix)
 **
 ** doubleBufferedMatrix Matrix
 **
 ** returns the number of columns in the matrix 
 **
 **
 ******************************************************/


int dbm_getCols(doubleBufferedMatrix Matrix){
  /* returns how many cols are currently in matrix */
  return(Matrix->cols);
}


/******************************************************
 **
 ** int dbm_getBufferCols(doubleBufferedMatrix Matrix)
 **
 ** doubleBufferedMatrix Matrix
 **
 ** Returns the number of columns in the column buffer or
 ** at least the maximum number allowed in the buffer.
 **
 ******************************************************/



int dbm_getBufferCols(doubleBufferedMatrix Matrix){
  /* returns how many columns are currently in the column buffer */

  return(Matrix->max_cols);

}

/******************************************************
 **
 ** int dbm_getBufferRows(doubleBufferedMatrix Matrix)
 **
 ** doubleBufferedMatrix Matrix
 **
 ** Returns the number of rows allowable in the
 ** row buffer (note that if the row buffer is not activated
 ** then this will not agree with the number of rows in the buffer
 ** currently which would be 0)
 **
 ******************************************************/


int dbm_getBufferRows(doubleBufferedMatrix Matrix){
  /* returns how many rows are currently in the row buffer */

  return(Matrix->max_rows);

}






/******************************************************
 **
 ** int dbm_getColumnValue(doubleBufferedMatrix Matrix, int *cols, double *value, int ncol)
 **
 ** doubleBufferedMatrix Matrix
 ** int *col - locations in matrix
 ** double *value - location to store value found in matrix (should have enough
 **                 space for rows worth of doubles).
 ** int ncol - number of columns
 **
 ** Returns 1 if get was successful. Otherwise returns 0 and *value is unchanged.
 **
 **
 ******************************************************/

int dbm_getValueColumn(doubleBufferedMatrix Matrix, int *cols, double *value, int ncols){

  double *tmp;
  int i,j;

  int curcol;


  for (j=0; j < ncols; j++){
    if ((cols[j] >= Matrix->cols) || (cols[j] < 0)){
      return 0;
    }
  }

  if (!Matrix->colmode){
    for (j= 0; j < ncols; j++){
      for (i =0; i < Matrix->rows; i++){
	tmp = dbm_internalgetValue(Matrix,i,cols[j]);
	value[j*Matrix->rows+ i] = *tmp; 
	Matrix->rowcolclash = 0; /* we are not setting anything here */
      }
    }
  } else {
    
    for (j= 0; j < ncols; j++){
      if (dbm_InColBuffer(Matrix,0,cols[j],&curcol)){
	memcpy(&value[j*Matrix->rows],&(Matrix->coldata[curcol][0]),Matrix->rows*sizeof(double));
      } else {
	if (!(Matrix->readonly))
	  dbm_FlushOldestColumn(Matrix); 
	dbm_LoadNewColumn(Matrix,cols[j]);
	memcpy(&value[j*Matrix->rows],&(Matrix->coldata[Matrix->max_cols -1][0]),Matrix->rows*sizeof(double));
      }
    }
  }
  
  return 1;
}




int dbm_getValueRow(doubleBufferedMatrix Matrix, int *rows, double *value, int nrows){

  double *tmp;
  int i,j;
  
  int *BufferContents;
  int *colsdone;
  
 
  for (i =0; i < nrows; i++){
    if ((rows[i] >= Matrix->rows) || (rows[i] < 0)){
      return 0;
    }
  }

  if (Matrix->colmode){
    if (Matrix->cols > Matrix->max_cols){ 

     /** First use what is already in the buffers **/

     BufferContents= dbm_whatsInColumnBuffer(Matrix); 
     colsdone = Calloc(Matrix->cols,int);
     
     for (j=0; j < Matrix->max_cols; j++){
       for (i=0; i < nrows; i++){
	 tmp = dbm_internalgetValue(Matrix,rows[i],BufferContents[j]);
	 value[BufferContents[j]*nrows + i] = *tmp; 
	 Matrix->rowcolclash = 0; /* we are not setting anything here */
       }
       colsdone[BufferContents[j]] = 1;
     }

     /** now do the rest by reading it in **/
     
     for (j=0; j < Matrix->cols; j++){
       if (colsdone[j] == 0){
	 for (i=0; i < nrows; i++){
	   tmp = dbm_internalgetValue(Matrix,rows[i],j);
	   value[j*nrows + i] = *tmp; 
	   Matrix->rowcolclash = 0; /* we are not setting anything here */
	 }
       }
     }

     Free(colsdone);
   } else {
     /* everything is already in memory, no need to read in */
     for (j =0; j < Matrix->cols; j++){
       for (i =0; i < nrows; i++){
	 tmp = dbm_internalgetValue(Matrix,rows[i],j);
	 value[j*nrows + i] = *tmp; 
	 Matrix->rowcolclash = 0; /* we are not setting anything here */
       }
     }
   }
  } else {
    for (i =0; i < nrows; i++){
      for (j =0; j < Matrix->cols; j++){
	tmp = dbm_internalgetValue(Matrix,rows[i],j);
	value[j*nrows + i] = *tmp; 
	Matrix->rowcolclash = 0; /* we are not setting anything here */
      }
    }
  }


  
  return 1;
}




int dbm_setValueColumn(doubleBufferedMatrix Matrix, int *cols, double *value, int ncols){

  double *tmp;
  int i,j;

  int curcol;
  
  if (Matrix->readonly){
    return 0; /* not successful */

  }
  
  for (j =0; j < ncols; j++){
    if ((cols[j] >= Matrix->cols) || (cols[j] < 0)){
      return 0;
    }
  }
  if (!Matrix->colmode){
    for (j=0; j < ncols; j++){
      for (i =0; i < Matrix->rows; i++){
	tmp = dbm_internalgetValue(Matrix,i,cols[j]);
	*tmp = value[j*Matrix->rows + i];
      }
    }
  } else {
    for (j= 0; j < ncols; j++){
      if (dbm_InColBuffer(Matrix,0,cols[j],&curcol)){
	memcpy(&(Matrix->coldata[curcol][0]),&value[j*Matrix->rows],Matrix->rows*sizeof(double));
      } else {
	if (!(Matrix->readonly))
	  dbm_FlushOldestColumn(Matrix); 
	dbm_LoadNewColumn_nofill(Matrix,cols[j]);
	memcpy(&(Matrix->coldata[Matrix->max_cols -1][0]),&value[j*Matrix->rows],Matrix->rows*sizeof(double));
      }
    }

    




  }

  return 1;
}






int dbm_setValueRow(doubleBufferedMatrix Matrix, int *rows, double *value, int nrows){

  double *tmp;
  int i,j;
    
  int *BufferContents;
  int *colsdone;
  
 

  if (Matrix->readonly){
    return 0; /* not successful */

  }
  
  for (i =0; i < nrows; i++){
    if ((rows[i] >= Matrix->rows) || (rows[i] < 0)){
      return 0;
    }
  }


  if (Matrix->colmode){
    if (Matrix->cols > Matrix->max_cols){ 
       
     /** First use what is already in the buffers **/

     BufferContents= dbm_whatsInColumnBuffer(Matrix); 
     colsdone = Calloc(Matrix->cols,int);
 
     for (j=0; j < Matrix->max_cols; j++){
       for (i=0; i < nrows; i++){
	 tmp = dbm_internalgetValue(Matrix,rows[i],BufferContents[j]);
	 *tmp = value[BufferContents[j]*nrows + i];
       }
       colsdone[BufferContents[j]] = 1;
     }

     /** now do the rest by reading it in **/
     
     for (j=0; j < Matrix->cols; j++){
       if (colsdone[j] == 0){
	 for (i=0; i < nrows; i++){
	   tmp = dbm_internalgetValue(Matrix,rows[i],j);
	   *tmp = value[j*nrows + i];
	 }
       }
     }

     Free(colsdone);
    } else {
      for (j =0; j < Matrix->cols; j++){  
	for (i =0; i < nrows; i++){
	  tmp = dbm_internalgetValue(Matrix,rows[i],j);
	  *tmp = value[j*nrows + i];
	}
      }
    }
  } else {
    for (i =0; i < nrows; i++){
      for (j =0; j < Matrix->cols; j++){
	tmp = dbm_internalgetValue(Matrix,rows[i],j);
	*tmp = value[j*nrows + i];
      }
    }
  }
  
  return 1;
}






char *dbm_getPrefix(doubleBufferedMatrix Matrix){

  char *returnvalue;
  int len= strlen(Matrix->fileprefix);

  returnvalue = Calloc(len+1,char);

  strcpy(returnvalue,Matrix->fileprefix);

  return returnvalue;
}



char *dbm_getDirectory(doubleBufferedMatrix Matrix){

  char *returnvalue;
  int len = strlen(Matrix->filedirectory);

  returnvalue = Calloc(len+1,char);

  strcpy(returnvalue,Matrix->filedirectory);

  return returnvalue;
}



char *dbm_getFileName(doubleBufferedMatrix Matrix, int col){
  
  char *returnvalue;
  int len = strlen(Matrix->filenames[col]);

  returnvalue = Calloc(len+1,char);

  strcpy(returnvalue,Matrix->filenames[col]);

  return returnvalue;



}




/* Changes the directory into which the temporary files are stored */

int dbm_setNewDirectory(doubleBufferedMatrix Matrix, const char *newdirectory){

  char *directory;
  char *olddirectory;
  char *tmp, *temp_name;
  int i;


  int len = strlen(newdirectory);

  directory = Calloc(len+1,char);

  strcpy(directory,newdirectory);

  olddirectory = Matrix->filedirectory;

  for (i =0; i < Matrix->cols; i++){
    temp_name = (char *)R_tmpnam(Matrix->fileprefix,newdirectory);
    tmp = Calloc(strlen(temp_name)+1,char);
    strcpy(tmp,temp_name);
    rename(Matrix->filenames[i], tmp);
    Matrix->filenames[i] = tmp;
    /*  Free(temp_name); */
  }

  Matrix->filedirectory = directory;

  Free(olddirectory);


  return 0;
}









int dbm_copyValues(doubleBufferedMatrix Matrix_target,doubleBufferedMatrix Matrix_source){

  int i, j;
  double *value, *tmp;


  if ((Matrix_source->rows != Matrix_target->rows) || (Matrix_source->cols != Matrix_target->cols)){
    return 0;
  }

  
  for (j=0; j < Matrix_source->cols; j++){
    for (i=0; i < Matrix_source->rows; i++){
      value = dbm_internalgetValue(Matrix_source,i,j);
      tmp = dbm_internalgetValue(Matrix_target,i,j);
      *tmp = *value;
    }
  }

  return 1;
}




int dbm_ewApply(doubleBufferedMatrix Matrix,double (* fn)(double, double *),double *fn_param){


  int i, j;
  double *value; 

  int *BufferContents;
  int *colsdone;
  


  if (Matrix->cols > Matrix->max_cols){  

    BufferContents= dbm_whatsInColumnBuffer(Matrix);
    colsdone = Calloc(Matrix->cols,int);
    
    /* Matrix doesn't have all the columns in the buffer */
    /* First do the columns currently in the buffer */
    for (j=0; j < Matrix->max_cols; j++){
      for (i=0; i < Matrix->rows; i++){
	value = dbm_internalgetValue(Matrix,i,BufferContents[j]);
	*value = fn(*value,fn_param);
      }
      colsdone[BufferContents[j]] = 1;
    }

    /* now read in  what we need to read in */
    for (j=0; j < Matrix->cols; j++){
      if (colsdone[j] == 0){
	for (i=0; i < Matrix->rows; i++){
	  value = dbm_internalgetValue(Matrix,i,j);
	  *value = fn(*value,fn_param);
	}
      }
    }
    

    Free(colsdone);
  } else {
    /* everything is in memory. Lets process it */
    for (j=0; j < Matrix->cols; j++){
      for (i=0; i < Matrix->rows; i++){
	value = dbm_internalgetValue(Matrix,i,j);
	*value = fn(*value,fn_param);
      }
    }
  
  }


  return 1;

}

  



double dbm_max(doubleBufferedMatrix Matrix,int naflag, int *foundfinite){



  int i, j;
  double *value;
  double max;
  
  int *BufferContents;
  int *colsdone;
  
  BufferContents= dbm_whatsInColumnBuffer(Matrix);

  colsdone = Calloc(Matrix->cols,int);

  *foundfinite=0;

  max = R_NegInf;
  if (Matrix->cols > Matrix->max_cols){
    /* Matrix doesn't have all the columns in the buffer */
    /* First do the columns currently in the buffer */

    for (j=0; j < Matrix->max_cols; j++){
      for (i=0; i < Matrix->rows; i++){
	value = dbm_internalgetValue(Matrix,i,BufferContents[j]);
	if (ISNAN(*value) && !naflag){
	  max= R_NaReal;
	  break;
	} 
	if (max < *value){
	  max=*value;
	  *foundfinite = 1;
	}
      }
      colsdone[BufferContents[j]] = 1;
    }
    
    /* now read in what we need to read in */
    for (j=0; j < Matrix->cols; j++){
      if (colsdone[j] == 0){
	for (i=0; i < Matrix->rows; i++){
	  value = dbm_internalgetValue(Matrix,i,j);
	  if (ISNAN(*value) && !naflag){
	    max= R_NaReal;
	    break;
	  } 
	  if (max < *value){
	    max=*value;
	    *foundfinite = 1;
	  }
	}
      }
    }
  } else {
    for (j=0; j < Matrix->cols; j++){
      for (i=0; i < Matrix->rows; i++){
	value = dbm_internalgetValue(Matrix,i,j);
	if (ISNAN(*value) && !naflag){
	  max= R_NaReal;
	  break;
	} 
	if (max < *value){
	  max=*value;
	  *foundfinite = 1;
	}
      }
    }
  }

  Free(colsdone);

  return max;
}


 

double dbm_min(doubleBufferedMatrix Matrix,int naflag, int *foundfinite){



  int i, j;
  double *value;
  double min;
  
  int *BufferContents;
  int *colsdone;
  
  BufferContents= dbm_whatsInColumnBuffer(Matrix);

  colsdone = Calloc(Matrix->cols,int);

  *foundfinite=0;
  
  min = R_PosInf; 

  if (Matrix->cols > Matrix->max_cols){
    /* Matrix doesn't have all the columns in the buffer */
    /* First do the columns currently in the buffer */
    for (j=0; j < Matrix->max_cols; j++){
      for (i=0; i < Matrix->rows; i++){
	value = dbm_internalgetValue(Matrix,i,BufferContents[j]);
	if (ISNAN(*value) && !naflag){
	  min= R_NaReal;
	  break;
	}
	if (min > *value){
	  min = *value;	
	  *foundfinite = 1;
	}
      }
      colsdone[BufferContents[j]] = 1;
    }
    
    /* now read in what we need to read in */
    for (j=0; j < Matrix->cols; j++){
      if (colsdone[j] == 0){
	for (i=0; i < Matrix->rows; i++){
	  value = dbm_internalgetValue(Matrix,i,j);
	  if (ISNAN(*value) && !naflag){
	    min= R_NaReal;
	    break;
	  }
	  if (min > *value){
	    min = *value;	
	    *foundfinite = 1;
	  }
	}
      }
    }



  } else {
    for (j=0; j < Matrix->cols; j++){
      for (i=0; i < Matrix->rows; i++){
	value = dbm_internalgetValue(Matrix,i,j);
	if (ISNAN(*value) && !naflag){
	  min= R_NaReal;
	  break;
	}
	if (min > *value){
	  min = *value;	
	  *foundfinite = 1;
	}
      }
    }
  }

  
  Free(colsdone);

  return min;
}
 



double dbm_mean(doubleBufferedMatrix Matrix,int naflag){



  int i, j;
  double *value;
  double mean=0.0;
  int count=0;
  
  int *BufferContents;
  int *colsdone;
  
  BufferContents= dbm_whatsInColumnBuffer(Matrix);

  colsdone = Calloc(Matrix->cols,int);

  if (Matrix->cols > Matrix->max_cols){
    /* Matrix doesn't have all the columns in the buffer */
    /* First do the columns currently in the buffer */
    for (j=0; j < Matrix->max_cols; j++){
      for (i=0; i < Matrix->rows; i++){
	value = dbm_internalgetValue(Matrix,i,BufferContents[j]);
	if (ISNAN(*value)){
	  if (!naflag){
	    mean= R_NaReal;
	    break;
	  }
	} else {
	  mean+= *value;
	  count++;
	}
      }
      
      colsdone[BufferContents[j]] = 1;
    }
   
    /* now read in what we need to read in */
    for (j=0; j < Matrix->cols; j++){
      if (colsdone[j] == 0){
	for (i=0; i < Matrix->rows; i++){
	  value = dbm_internalgetValue(Matrix,i,j);
	  if (ISNAN(*value)){
	    if (!naflag){
	      mean= R_NaReal;
	      break;
	    }
	  } else {
	    mean+= *value;
	    count++;
	  }
	}
      }
    }
  } else {
    for (j=0; j < Matrix->cols; j++){
      for (i=0; i < Matrix->rows; i++){
	value = dbm_internalgetValue(Matrix,i,j);
	if (ISNAN(*value)){
	  if (!naflag){
	    mean= R_NaReal;
	    break;
	  }
	} else {
	  mean+= *value;
	  count++;
	}
      }
    }
  }
  
  Free(colsdone);

  return mean/(double)(count);
}
 



double dbm_sum(doubleBufferedMatrix Matrix,int naflag){



  int i, j;
  double *value;
  double sum=0.0;
 
  int *BufferContents;
  int *colsdone;
  
  BufferContents= dbm_whatsInColumnBuffer(Matrix);

  colsdone = Calloc(Matrix->cols,int);
  
  if (Matrix->cols > Matrix->max_cols){
    /* Matrix doesn't have all the columns in the buffer */
    /* First do the columns currently in the buffer */
    for (j=0; j < Matrix->max_cols; j++){
      for (i=0; i < Matrix->rows; i++){
	value = dbm_internalgetValue(Matrix,i,BufferContents[j]);
	if (ISNAN(*value)){
	  if (!naflag){
	    sum= R_NaReal;
	    break;
	  }
	} else {
	  sum+= *value;
	}
      }
      colsdone[BufferContents[j]] = 1;
    }
    
    /* now read in what we need to read in */
    for (j=0; j < Matrix->cols; j++){
      if (colsdone[j] == 0){
	for (i=0; i < Matrix->rows; i++){
	  value = dbm_internalgetValue(Matrix,i,j);
	  if (ISNAN(*value)){
	    if (!naflag){
	      sum= R_NaReal;
	      break;
	    }
	  } else {
	    sum+= *value;
	    }
	}
      }
    }
  } else {
    for (j=0; j < Matrix->cols; j++){
      for (i=0; i < Matrix->rows; i++){
	value = dbm_internalgetValue(Matrix,i,j);
	if (ISNAN(*value)){
	  if (!naflag){
	    sum= R_NaReal;
	    break;
	  }
	} else {
	  sum+= *value;
	}
      }
    }
  }
  
  Free(colsdone);
  
  return sum;
}
 

double dbm_var(doubleBufferedMatrix Matrix,int naflag){



  int i, j;
  double *value;
  double s2=0.0;
  double mean = 0.0;
  long long count=1;
  int firstnonNAfound =0;

  int *BufferContents;
  int *colsdone;
  
  BufferContents= dbm_whatsInColumnBuffer(Matrix);

  colsdone = Calloc(Matrix->cols,int);
  
  if (Matrix->cols > Matrix->max_cols){
    /* Matrix doesn't have all the columns in the buffer */
    /* First do the columns currently in the buffer */
    for (j=0; j < Matrix->max_cols; j++){
      for (i=0; i < Matrix->rows; i++){
	value = dbm_internalgetValue(Matrix,i,BufferContents[j]);
	if (ISNAN(*value)){
	  if (!naflag){
	    s2= R_NaReal;
	    break;
	  }
	} else {
	  if (firstnonNAfound){ 
	    count++;
	    s2 = s2 + (double)(count-1)*(*value - mean)*(*value - mean)/(double)(count);
	    mean = mean + (*value - mean)/(double)(count);
	  } else {
	    mean = *dbm_internalgetValue(Matrix,i,BufferContents[j]);
	  }
	  firstnonNAfound =1;
	}
      }
      colsdone[BufferContents[j]] = 1;
    }

    /* now read in what we need to read in */
    for (j=0; j < Matrix->cols; j++){
      if (colsdone[j] == 0){
	for (i=0; i < Matrix->rows; i++){
	  value = dbm_internalgetValue(Matrix,i,j);
	  if (ISNAN(*value)){
	    if (!naflag){
	      s2= R_NaReal;
	      break;
	    }
	  } else {
	    if (firstnonNAfound){ 
	      count++;
	      s2 = s2 + (double)(count-1)*(*value - mean)*(*value - mean)/(double)(count);
	      mean = mean + (*value - mean)/(double)(count);
	    } else {
	      mean = *dbm_internalgetValue(Matrix,i,j);
	    }
	    firstnonNAfound =1;
	  }
	}
      }
    }
    
  } else {
    for (j=0; j < Matrix->cols; j++){
      for (i=0; i < Matrix->rows; i++){
	value = dbm_internalgetValue(Matrix,i,j);
	if (ISNAN(*value)){
	  if (!naflag){
	    s2= R_NaReal;
	    break;
	  }
	} else {
	  if (firstnonNAfound){ 
	    count++;
	    s2 = s2 + (double)(count-1)*(*value - mean)*(*value - mean)/(double)(count);
	    mean = mean + (*value - mean)/(double)(count);
	  } else {
	    mean = *dbm_internalgetValue(Matrix,i,j);
	  }
	  firstnonNAfound =1;
	}
      }
    }
  }
  
  Free(colsdone);
  
  if (firstnonNAfound){
    return s2/(double)(count-1);  /* -1 for sample sd and -1 for overcount on last iteration */
  } else {
    return R_NaReal;
  }
  
}
 







void dbm_rowMeans(doubleBufferedMatrix Matrix,int naflag,double *results){

  int i,j;
  double *value;
  int *counts = Calloc(Matrix->rows,int);
  int *foundNA = Calloc(Matrix->rows,int);

  
  
  for (i=0; i < Matrix->rows; i++){
    results[i] =0.0;
  }

  for (j=0; j < Matrix->cols; j++){
    for (i=0; i < Matrix->rows; i++){
      value = dbm_internalgetValue(Matrix,i,j);
      if (ISNAN(*value)){
	if (!naflag){
	  foundNA[i] = 1;
	}
      } else {
	results[i]+=*value;
	counts[i]+=1;
      }
    }
  }

  for (i=0; i < Matrix->rows; i++){
    if (foundNA[i]){
      results[i] = R_NaReal;
    } else {
      results[i]/=(double)counts[i];
    }
  }


  Free(counts);
  Free(foundNA);


}




void dbm_rowSums(doubleBufferedMatrix Matrix,int naflag,double *results){

  int i,j;
  double *value;
  int *foundNA = Calloc(Matrix->rows,int);
  
  
  
  for (i=0; i < Matrix->rows; i++){
    results[i] =0.0;
  }

  for (j=0; j < Matrix->cols; j++){
    for (i=0; i < Matrix->rows; i++){
      value = dbm_internalgetValue(Matrix,i,j);
      if (ISNAN(*value)){
	if (!naflag){
	  foundNA[i] = 1;
	}
      } else {
	results[i]+=*value;
      }
    }
  }

  for (i=0; i < Matrix->rows; i++){
    if (foundNA[i]){
      results[i] = R_NaReal;
    } 
  }

  Free(foundNA);


}



static void dbm_singlecolMeans(doubleBufferedMatrix Matrix,int j,int naflag,double *results){

  int i;
  double *value;
  int foundNA=0;
  int counts=0; 
  
  results[j] = 0.0;


  for (i=0; i < Matrix->rows; i++){
    value = dbm_internalgetValue(Matrix,i,j);
    if (ISNAN(*value)){
      if (!naflag){
	foundNA = 1;
	break;
      }
    } else {
      results[j]+=*value;
      counts++;
    }
  }
  
  if (foundNA){
    results[j] = R_NaReal;
  } else {
    results[j]/=(double)counts;
  }

}



void dbm_colMeans(doubleBufferedMatrix Matrix,int naflag,double *results){

  int j;

  int *BufferContents;
  int *colsdone;
  
  BufferContents= dbm_whatsInColumnBuffer(Matrix);

  colsdone = Calloc(Matrix->cols,int);

  if (Matrix->cols > Matrix->max_cols){
    /* Matrix doesn't have all the columns in the buffer */

    /* First do the columns currently in the buffer */
    for (j=0; j < Matrix->max_cols; j++){
      dbm_singlecolMeans(Matrix,BufferContents[j],naflag,results);
      colsdone[BufferContents[j]] = 1;
    }
    
    /* now read in what we need to read in */
    for (j=0; j < Matrix->cols; j++){
      if (colsdone[j] == 0){
	dbm_singlecolMeans(Matrix,j,naflag,results);
      }
    }
  } else {
    for (j=0; j < Matrix->cols; j++){
      dbm_singlecolMeans(Matrix,j,naflag,results);
    }
  }
  Free(colsdone);


}


static void dbm_singlecolSums(doubleBufferedMatrix Matrix,int j,int naflag,double *results){
  int i;
  double *value;


  results[j] = 0.0;

  for (i=0; i < Matrix->rows; i++){
    value = dbm_internalgetValue(Matrix,i,j);
    if (ISNAN(*value)){
      if (!naflag){
	results[j] = R_NaReal;
	return;
      }
    } else {
      results[j]+=*value;
    }
  }
}






void dbm_colSums(doubleBufferedMatrix Matrix,int naflag,double *results){

  int j;
 
  int *BufferContents;
  int *colsdone;
  
  BufferContents= dbm_whatsInColumnBuffer(Matrix);

  colsdone = Calloc(Matrix->cols,int);

  if (Matrix->cols > Matrix->max_cols){
    /* Matrix doesn't have all the columns in the buffer */

    /* First do the columns currently in the buffer */
    for (j=0; j < Matrix->max_cols; j++){
      dbm_singlecolSums(Matrix,BufferContents[j],naflag,results);
      colsdone[BufferContents[j]] = 1;
    }
    
    /* now read in what we need to read in */
    for (j=0; j < Matrix->cols; j++){
      if (colsdone[j] == 0){
	dbm_singlecolSums(Matrix,j,naflag,results);
      }
    }
  } else {
    for (j=0; j < Matrix->cols; j++){
      dbm_singlecolSums(Matrix,j,naflag,results);
    }
  }
  Free(colsdone);
}






void dbm_rowVars(doubleBufferedMatrix Matrix,int naflag,double *results){

  int i,j;
  double *value;
  int *counts = Calloc(Matrix->rows,int);
  int *foundNA = Calloc(Matrix->rows,int);
  double *means = Calloc(Matrix->rows,double);
  
  
  
  for (i=0; i < Matrix->rows; i++){
    means[i] = *dbm_internalgetValue(Matrix,i,0);
    if (ISNAN(means[i])){
      foundNA[i]+= 1;
      means[i] = 0.0; 
      results[i] =0.0;
      counts[i] = 1;
    } else {
      results[i] =0.0;
      counts[i] = 2;
    }
  }

  for (j=1; j < Matrix->cols; j++){
    for (i=0; i < Matrix->rows; i++){
      value = dbm_internalgetValue(Matrix,i,j);
      if (ISNAN(*value)){
	foundNA[i]+=1;
      } else {
	results[i] = results[i] + (double)(counts[i] -1)*(*value - means[i])*(*value - means[i])/(double)(counts[i]);
	means[i] = means[i] + (*value - means[i])/(double)counts[i];
	counts[i]++;
      }
    }
  }

  for (i=0; i < Matrix->rows; i++){ 
    /* Rprintf("found NA: %d  Counts: %d\n",foundNA[i],counts[i]); */
    if (foundNA[i] == Matrix->cols){
      results[i] = R_NaReal;
    } else {
      if (counts[i] > 2){
	results[i]/=(double)(counts[i]-2);    /* -1 for sample, -1 for overcount*/
      } else {
	results[i] = R_NaReal;
      }
    }
  }

  Free(means);
  Free(counts);
  Free(foundNA);
}


static void dbm_singlecolVars(doubleBufferedMatrix Matrix,int j,int naflag,double *results){
  int i;
  double *value;
  
  int foundNA=0;
  int counts=0;
  double means;



    means = *dbm_internalgetValue(Matrix,0,j); 
    if (ISNAN(means)){ 
      if (!naflag){
	results[j] = R_NaReal;
	return;
      }

      foundNA+= 1;
      results[j] = 0.0;
      means = 0.0;
      counts=1;
    } else {
      results[j] =0.0;
      counts = 2;
    }
    for (i=1; i < Matrix->rows; i++){
      value = dbm_internalgetValue(Matrix,i,j);  
      if (ISNAN(*value)){
	if (!naflag){
	  results[j] = R_NaReal;
	  return;
	}

	foundNA += 1; 
      } else {
	results[j] = results[j] + (double)(counts -1)*(*value - means)*(*value - means)/(double)(counts);
	means = means + (*value - means)/(double)counts;
	counts++;
      }
    }

    if (foundNA == Matrix->rows){
      results[j] = R_NaReal;
    } else {
      if (counts > 2){
	results[j]/=(double)(counts-2); /* -1 for sample, -1 for overcounting */ 
      } else {
	results[j] = R_NaReal;
      }
    }
    
}



void dbm_colVars(doubleBufferedMatrix Matrix,int naflag,double *results){
  int j;
 

  int *BufferContents;
  int *colsdone;
  
  BufferContents= dbm_whatsInColumnBuffer(Matrix);

  colsdone = Calloc(Matrix->cols,int);

  if (Matrix->cols > Matrix->max_cols){
    /* Matrix doesn't have all the columns in the buffer */

    /* First do the columns currently in the buffer */
    for (j=0; j < Matrix->max_cols; j++){
      dbm_singlecolVars(Matrix,BufferContents[j],naflag,results);
      colsdone[BufferContents[j]] = 1;
    }
    
    /* now read in what we need to read in */
    for (j=0; j < Matrix->cols; j++){
      if (colsdone[j] == 0){
	dbm_singlecolVars(Matrix,j,naflag,results);
      }
    }
  } else {
    for (j=0; j < Matrix->cols; j++){
      dbm_singlecolVars(Matrix,j,naflag,results);
    }
  }
  Free(colsdone);
}






/*********************************************************************
 **
 ** void dbm_rowMax(doubleBufferedMatrix Matrix,int naflag,double *results)
 **
 ** 
 **
 ** Find the maximum in each row
 **
 ** Special cases
 ** - all elements in ROW are NA
 **
 **
 *********************************************************************/

void dbm_rowMax(doubleBufferedMatrix Matrix,int naflag,double *results){

  int i,j;
  double *value;

  int *isNA = Calloc(Matrix->rows,int);

  for (i=0; i < Matrix->rows; i++){
    results[i] = *dbm_internalgetValue(Matrix,i,0);
    if (ISNAN(results[i])){
      if (!naflag){
	results[i] = R_NaReal;
      } else {
	results[i] = R_NegInf;
      }
      isNA[i] = 1;
    }
  }

  for (j=1; j < Matrix->cols; j++){
    for (i=0; i < Matrix->rows; i++){
      value = dbm_internalgetValue(Matrix,i,j);
      if (ISNAN(*value)){
	if (!naflag){
	  results[i] = R_NaReal;
	}
      } else {
	if (results[i] < *value){
	  results[i]=*value;
	}
	if (isNA[i]){
	  isNA[i] = 0;
	}
      }
    }
  }

  for (i=0; i < Matrix->rows; i++){
    if (isNA[i]){
      results[i]=  R_NaReal;
    }
  }


  
  Free(isNA);
  
}




static void dbm_singlecolMax(doubleBufferedMatrix Matrix,int j, int naflag,double *results){
  
  int i;
  double *value;
  
  results[j] = *dbm_internalgetValue(Matrix,0,j);
  if (ISNAN(results[j])){
    if (!naflag){
      results[j] = R_NaReal;
      return;
    } else {
      results[j] = R_NegInf;
    }
  }
  for (i=1; i < Matrix->rows; i++){
    value = dbm_internalgetValue(Matrix,i,j);
    if (ISNAN(*value)){
      if (!naflag){
	results[j] = R_NaReal;
	return;
      }
    } else {
      if (results[j] < *value){
	results[j]=*value;
      }
    }
  }
}


void dbm_colMax(doubleBufferedMatrix Matrix,int naflag,double *results){
  int j;
      


  int *BufferContents;
  int *colsdone;
  
  BufferContents= dbm_whatsInColumnBuffer(Matrix);

  colsdone = Calloc(Matrix->cols,int);

  if (Matrix->cols > Matrix->max_cols){
    /* Matrix doesn't have all the columns in the buffer */

    /* First do the columns currently in the buffer */
    for (j=0; j < Matrix->max_cols; j++){
      dbm_singlecolMax(Matrix,BufferContents[j],naflag,results);
      colsdone[BufferContents[j]] = 1;
    }
    
    /* now read in what we need to read in */
    for (j=0; j < Matrix->cols; j++){
      if (colsdone[j] == 0){
	dbm_singlecolMax(Matrix,j,naflag,results);
      }
    }
  } else {
    for (j=0; j < Matrix->cols; j++){
      dbm_singlecolMax(Matrix,j,naflag,results);
    }
  }
  Free(colsdone);
}















void dbm_rowMin(doubleBufferedMatrix Matrix,int naflag,double *results){

  int i,j;
  double *value;

  int *isNA = Calloc(Matrix->rows,int);

  for (i=0; i < Matrix->rows; i++){
    results[i] = *dbm_internalgetValue(Matrix,i,0);
    if (ISNAN(results[i])){
      if (!naflag){
	results[i] = R_NaReal;
      } else {
	results[i] = R_PosInf;
      }
      isNA[i] = 1;
    }
  }

  for (j=1; j < Matrix->cols; j++){
    for (i=0; i < Matrix->rows; i++){
      value = dbm_internalgetValue(Matrix,i,j);
      if (ISNAN(*value)){
	if (!naflag){
	  results[i] = R_NaReal;
	}
      } else {
	if (results[i] > *value){
	  results[i]=*value;
	}
	if (isNA[i]){
	  isNA[i] = 0;
	}
      }
    }
  }

  for (i=0; i < Matrix->rows; i++){
    if (isNA[i]){
      results[i]=  R_NaReal;
    }
  }


  
  Free(isNA);
  
}




static void dbm_singlecolMin(doubleBufferedMatrix Matrix,int j,int naflag,double *results){
  int i;
  double *value;
  
  results[j] = *dbm_internalgetValue(Matrix,0,j);
  if (ISNAN(results[j])){
    if (!naflag){
      results[j] = R_NaReal;
      return;
    } else {
      results[j] = R_PosInf;
    }
  }
  for (i=1; i < Matrix->rows; i++){
      value = dbm_internalgetValue(Matrix,i,j);
      if (ISNAN(*value)){
	if (!naflag){
	  results[j] = R_NaReal;
	  return;
	}
      } else {
	if (results[j] > *value){
	  results[j]=*value;
	}
      }
  }
}



void dbm_colMin(doubleBufferedMatrix Matrix,int naflag,double *results){
  int j;
 
  int *BufferContents;
  int *colsdone;
  
  BufferContents= dbm_whatsInColumnBuffer(Matrix);

  colsdone = Calloc(Matrix->cols,int);

  if (Matrix->cols > Matrix->max_cols){
    /* Matrix doesn't have all the columns in the buffer */

    /* First do the columns currently in the buffer */
    for (j=0; j < Matrix->max_cols; j++){
      dbm_singlecolMin(Matrix,BufferContents[j],naflag,results);
      colsdone[BufferContents[j]] = 1;
    }
    
    /* now read in what we need to read in */
    for (j=0; j < Matrix->cols; j++){
      if (colsdone[j] == 0){
	dbm_singlecolMin(Matrix,j,naflag,results);
      }
    }
  } else {
    for (j=0; j < Matrix->cols; j++){
      dbm_singlecolMin(Matrix,j,naflag,results);
    }
  }
  Free(colsdone);

}


/**********************************************************
 **
 ** int sort_double(const void *a1,const void *a2)
 **
 ** a comparison function used when sorting doubles.
 **
 **********************************************************/

static int sort_double(const double *a1,const double *a2){
  if (*a1 < *a2)
    return (-1);
  if (*a1 > *a2)
    return (1);
  return 0;
}




static void dbm_singlecolMedian(doubleBufferedMatrix Matrix,int j,int naflag,double *results){

  int i, i_nonNA=0;
  double *value;
  double *buffer = Calloc(Matrix->rows,double);
  

  for (i=0; i < Matrix->rows; i++){
    value = dbm_internalgetValue(Matrix,i,j);
    if (ISNAN(*value)){
      if (!naflag){
	Free(buffer);
	results[j] = R_NaReal;
	return;
      } 
    } else {
      buffer[i_nonNA] = *value;
      i_nonNA++;
    }
  }

  
  if (i_nonNA == 0){
    results[j] = R_NaReal;
  } else if ((i_nonNA % 2) == 1){
    rPsort(buffer, i_nonNA, (i_nonNA-1)/2);
    results[j]= buffer[(i_nonNA-1)/2];
  } else {
    rPsort(buffer, i_nonNA, (i_nonNA)/2);
    results[j]= buffer[(i_nonNA)/2];
    rPsort(buffer, i_nonNA, (i_nonNA)/2 - 1);
    results[j]= (results[j] + buffer[(i_nonNA)/2 -1])/2;

  }



  /*  
      qsort(buffer,i_nonNA,sizeof(double),(int(*)(const void*, const void*))sort_double);
  
  
      if ((i_nonNA % 2) == 1){
      results[j] = buffer[(i_nonNA-1)/2];
      } else { 
      results[j] = (buffer[(i_nonNA)/2-1] + buffer[(i_nonNA)/2])/2.0;
      }
  */
  Free(buffer);


}




void dbm_colMedians(doubleBufferedMatrix Matrix,int naflag,double *results){
  int j;
 
  int *BufferContents;
  int *colsdone;
  
  BufferContents= dbm_whatsInColumnBuffer(Matrix);

  colsdone = Calloc(Matrix->cols,int);

  if (Matrix->cols > Matrix->max_cols){
    /* Matrix doesn't have all the columns in the buffer */

    /* First do the columns currently in the buffer */
    for (j=0; j < Matrix->max_cols; j++){
      dbm_singlecolMedian(Matrix,BufferContents[j],naflag,results);
      colsdone[BufferContents[j]] = 1;
    }
    
    /* now read in what we need to read in */
    for (j=0; j < Matrix->cols; j++){
      if (colsdone[j] == 0){
	dbm_singlecolMedian(Matrix,j,naflag,results);
      }
    }
  } else {
    for (j=0; j < Matrix->cols; j++){
      dbm_singlecolMedian(Matrix,j,naflag,results);
    }
  }
  Free(colsdone);

}




static void dbm_singlecolRange(doubleBufferedMatrix Matrix,int j,int naflag,int finite, double *results){

  int i,start_ind;
  double *value, *value1;
  
  /* Min is stored in results[0 , 2, ... 2*(Matrix->cols-1)] */
  /* Max is stored in results[1, 3,  ... ,2*Matrix->cols -1] */


  results[j*2] = *dbm_internalgetValue(Matrix,0,j);
  results[j*2+1] = results[j*2];

  if (ISNAN(results[j*2])){
    if (!naflag){
      results[j*2] = R_NaReal;
      results[j*2 + 1] = R_NaReal;
      return;
    } else {
      results[j*2] = R_PosInf;
      results[j*2 + 1] = R_NegInf;
    }
    
  }
  if ((Matrix->rows %2) ==0){
    start_ind = 0;
  } else {
    start_ind = 1;
  }




  for (i=start_ind; i < Matrix->rows; i=i+2){
      value = dbm_internalgetValue(Matrix,i,j);
      value1 = dbm_internalgetValue(Matrix,i+1,j);
      if (ISNAN(*value) || ISNAN(*value1)){
	if (!naflag){
	  results[j*2] = R_NaReal;
	  results[j*2 + 1] = R_NaReal;
	  return;
	} else if (ISNAN(*value) && !ISNAN(*value1)){
	  /* value1 is the non NA one */
	  if (*value1 > results[j*2 + 1]){
	    results[j*2 + 1] = *value1;
	  }
	  if (*value1 < results[j*2]){
	    results[j*2] = *value1;
	  }
	}  else {
	  /* value is the non NA one */
	  if (*value > results[j*2 + 1]){
	    results[j*2 + 1] = *value;
	  }
	  if (*value < results[j*2]){
	    results[j*2] = *value;
	  }
	}
      } else {
	
	if (*value < *value1){
	  if (*value < results[j*2]){
	    results[j*2] = *value;
	  }
	  if (*value1 > results[j*2 + 1]){
	    results[j*2 + 1] = *value1;
	  }
	} else {
	  if (*value > results[j*2 + 1]){
	    results[j*2 + 1] = *value;
	  }
	  if (*value1 < results[j*2]){
	    results[j*2] = *value1;
	  }
	}
      }
  }
}



void dbm_colRanges(doubleBufferedMatrix Matrix,int naflag, int finite, double *results){
  int j;
 
  int *BufferContents;
  int *colsdone;
  
  BufferContents= dbm_whatsInColumnBuffer(Matrix);

  colsdone = Calloc(Matrix->cols,int);

  if (Matrix->cols > Matrix->max_cols){
    /* Matrix doesn't have all the columns in the buffer */

    /* First do the columns currently in the buffer */
    for (j=0; j < Matrix->max_cols; j++){
      dbm_singlecolRange(Matrix,BufferContents[j],naflag,finite,results);
      colsdone[BufferContents[j]] = 1;
    }
    
    /* now read in what we need to read in */
    for (j=0; j < Matrix->cols; j++){
      if (colsdone[j] == 0){
	dbm_singlecolRange(Matrix,j,naflag,finite,results);
      }
    }
  } else {
    for (j=0; j < Matrix->cols; j++){
      dbm_singlecolRange(Matrix,j,naflag,finite,results);
    }
  }
  Free(colsdone);

}








int dbm_memoryInUse(doubleBufferedMatrix Matrix){


  int i;
  int object_size =0;

  /* this is the size of the storage object itself */
  object_size+= 10*sizeof(int) + 3*sizeof(double**) + 2*sizeof(char *) + sizeof(int *) + sizeof(char **);

  /* Now start adding in things that are of variable size and stored in the object */

  /* first deal with the column buffer */
  
  if (Matrix->cols < Matrix->max_cols){
    object_size+= Matrix->cols*sizeof(double *);
    object_size+= Matrix->cols*Matrix->rows*sizeof(double);
    object_size+= Matrix->cols*sizeof(int);
  } else {
    object_size+= Matrix->max_cols*sizeof(double *);
    object_size+= Matrix->max_cols*Matrix->rows*sizeof(double);
    object_size+= Matrix->max_cols*sizeof(int);
  }

  /* Now the row buffer */
  if (!Matrix->colmode){
    object_size+= Matrix->cols*sizeof(double *);
    if (Matrix->rows < Matrix->max_rows){
      object_size+= Matrix->rows*Matrix->max_rows*sizeof(double);
    } else {
      object_size+= Matrix->cols*Matrix->max_rows*sizeof(double);
    }
  }
  
  
  /* the strings */
  object_size+=strlen(Matrix->fileprefix) + 1;
  object_size+=strlen(Matrix->filedirectory) + 1;
  
  object_size+= Matrix->cols*sizeof(char *);
  for (i=0; i < Matrix->cols; i++){
    object_size+=strlen(Matrix->filenames[i]) +1;
  }
  
  
  return object_size;






}



double dbm_fileSpaceInUse(doubleBufferedMatrix Matrix){
  
  return (double)(Matrix->rows)*(double)Matrix->cols*(double)sizeof(double);

}



/* Note that this function only works when in row mode  (in any level of efficiency) */

void dbm_rowMedians(doubleBufferedMatrix Matrix,int naflag,double *results){
  int i; /* indexes current row */
  int j; /* indexes cols */
 
  double *buffer = Calloc(Matrix->cols,double);
  int j_nonNA=0;
  double *value;


  for (i = 0; i < Matrix->rows; i++){
    
    j_nonNA = 0;
    for (j = 0; j < Matrix->cols; j++){
      value = dbm_internalgetValue(Matrix,i,j);
      if (ISNAN(*value)){
	if (!naflag){
	  results[i] = R_NaReal;
	  break;
	} 
      } else {
	buffer[j_nonNA] = *value;
	j_nonNA++;
      }
    }
    if (j_nonNA == 0){
      results[i] = R_NaReal;
    } else if ((j_nonNA % 2) == 1){
      rPsort(buffer, j_nonNA, (j_nonNA-1)/2);
      results[i]= buffer[(j_nonNA-1)/2];
    } else {
      rPsort(buffer, j_nonNA, (j_nonNA)/2);
      results[i]= buffer[(j_nonNA)/2];
      rPsort(buffer, j_nonNA, (j_nonNA)/2 - 1);
      results[i]= (results[j] + buffer[(j_nonNA)/2 -1])/2;
    }
  }
  
  Free(buffer);
  
}

