/*****************************************************
 **
 ** file: RBufferedMatrix.c
 **
 ** Copyright (C) 2006    B. M. Bolstad
 **
 ** aim: c level R-interface for BufferedMatrix objects.
 **
 **
 **  History
 **  Feb 2, 2006 Initial version
 **  Feb 3, 2006 Add Finalizer
 **  Feb 7, 2006 Add functionality for accessing columns or rows at a time
 **              and returning as an R matrix
 **  Feb 16, 2006 R_bm_getPrefix, R_bm_getDirectory, R_copyValues added
 **  Feb 17, 2006 R_bm_ewApply added, R_bm_ewLog, R_bm_ewPow, R_bm_ewSqrt, R_bm_ewExp added
 **  Feb 22, 2006 R_bm_max, R_bm_min, R_bm_sum, R_bm_var, R_bm_mean, R_bm_ColSums, R_bm_ColMeans, R_bm_RowMeans,  R_bm_RowSums
 **
 *****************************************************/

#include "doubleBufferedMatrix.h"

#include <Rdefines.h>
#include <Rinternals.h>

#include <math.h>


/* Pre-declare the function which deallocated the c part of the BufferedMatrix */

SEXP R_bm_Destroy(SEXP R_BufferedMatrix);


/*****************************************************
 **
 ** static void R_bm_Finalizer(SEXP R_BufferedMatrix)
 **
 ** This is the Finalizer function that is called 
 ** when the object is deleted on gc() or when R quits.
 ** It deallocates everything and deletes all the 
 ** temporary files etc
 **
 *****************************************************/



static void R_bm_Finalizer(SEXP R_BufferedMatrix){

  R_bm_Destroy(R_BufferedMatrix);

}




/*****************************************************
 **
 ** SEXP R_bm_Create(SEXP R_prefix, SEXP R_directory, SEXP R_max_rows, SEXP R_max_cols)
 **
 ** SEXP R_prefix - a character string to be used for start of any temporary files created
 ** SEXP R_directory - a character string giving the path where temporary files should be stored
 ** SEXP R_max_rows, R_max_cols - buffer size 
 **
 ** Creates a Buffered Matrix object and returns a pointer to
 ** it. Note that initially the matrix is empty.
 **  The number of rows must be set and columns added before 
 ** the matrix is really of any use.
 **
 ** Returns a pointer to the object
 **
 *****************************************************/


SEXP R_bm_Create(SEXP R_prefix, SEXP R_directory, SEXP R_max_rows, SEXP R_max_cols){

  char *prefix = CHAR(VECTOR_ELT(R_prefix,0));
  char *directory = CHAR(VECTOR_ELT(R_directory,0));

  double max_rows = asReal(R_max_rows);
  double max_cols = asReal(R_max_cols);

  SEXP val;

  doubleBufferedMatrix Matrix;

  Matrix = dbm_alloc(max_rows,max_cols,prefix,directory);

  PROTECT(val = R_MakeExternalPtr(Matrix, R_NilValue, R_NilValue));

  R_RegisterCFinalizerEx(val, (R_CFinalizer_t)R_bm_Finalizer,TRUE);


  UNPROTECT(1);
  return val;
}



SEXP R_bm_Test_C(SEXP R_BufferedMatrix){

  int i,j;
  double temp;

  doubleBufferedMatrix Matrix;

  Matrix =  R_ExternalPtrAddr(R_BufferedMatrix);
  
  if (Matrix == NULL){
    return R_BufferedMatrix;
  }


  dbm_setRows(Matrix,5);
  
  for (i = 0; i < 5; i++){
    dbm_AddColumn(Matrix);
  }
  

  Rprintf("Checking dimensions\n");
  Rprintf("Rows: %d\n",dbm_getRows(Matrix));
  Rprintf("Cols: %d\n",dbm_getCols(Matrix));
  Rprintf("Buffer Rows: %d\n",dbm_getBufferRows(Matrix));
  Rprintf("Buffer Cols: %d\n",dbm_getBufferCols(Matrix));

  Rprintf("\n");
  Rprintf("Assigning Values\n");
  for (i =0; i < 5; i++){
    for (j=0; j < 5; j++){
      dbm_setValue(Matrix,i,j,(double)(i+j));
    }
  }

  for (i=0; i < 5; i++){
    for (j=0; j < 5; j++){
      dbm_getValue(Matrix,i,j,&temp);
      Rprintf("%f ",temp);
    }
    Rprintf("\n");
  }
  Rprintf("\n");

  
  return R_BufferedMatrix;
}



SEXP R_bm_Test_C2(SEXP R_BufferedMatrix){


  int i,j;
  double temp;

  doubleBufferedMatrix Matrix;

  Matrix =  R_ExternalPtrAddr(R_BufferedMatrix);
  
  if (Matrix == NULL){
    return R_BufferedMatrix;
  }

  Rprintf("Checking dimensions\n");
  Rprintf("Rows: %d\n",dbm_getRows(Matrix));
  Rprintf("Cols: %d\n",dbm_getCols(Matrix));
  Rprintf("Buffer Rows: %d\n",dbm_getBufferRows(Matrix));
  Rprintf("Buffer Cols: %d\n",dbm_getBufferCols(Matrix));

  Rprintf("\n");
  Rprintf("Printing Values\n");
  for (i =0; i <dbm_getRows(Matrix); i++){
    for (j=0; j < dbm_getCols(Matrix); j++){
      dbm_getValue(Matrix,i,j,&temp);
      Rprintf("%f ",temp);
    }
    Rprintf("\n");
  }
  Rprintf("\n");

  return R_BufferedMatrix;

}



/*****************************************************
 **
 ** SEXP R_bm_Destroy(SEXP R_BufferedMatrix)
 **
 ** SEXP R_BufferedMatrix
 **
 ** Deallocs a BufferedMatrix
 **
 ** Should return a null pointer
 **
 **
 *****************************************************/

SEXP R_bm_Destroy(SEXP R_BufferedMatrix){

  doubleBufferedMatrix Matrix;
  
  Matrix =  R_ExternalPtrAddr(R_BufferedMatrix);
  if (Matrix != NULL){
    dbm_free(Matrix);
  }
  
  
  R_BufferedMatrix = R_MakeExternalPtr(NULL, R_NilValue, R_NilValue);

  return R_BufferedMatrix;


}

/*****************************************************
 **
 ** SEXP R_bm_setRows(SEXP R_BufferedMatrix, SEXP R_rows)
 **
 ** SEXP R_BufferedMatrix
 ** SEXP R_rows - number of rows in the matrix
 **
 ** Sets the number of rows in the matrix. Once this is set
 ** it can not be altered.
 **
 ** RETURNS TRUE if operation was successful
 **         FALSE if operation did not succeed.
 **
 *****************************************************/

SEXP R_bm_setRows(SEXP R_BufferedMatrix, SEXP R_rows){

  SEXP returnvalue;

  doubleBufferedMatrix Matrix;
  int rows;

  PROTECT(returnvalue=allocVector(LGLSXP,1));


  Matrix =  R_ExternalPtrAddr(R_BufferedMatrix);
 
  if (Matrix == NULL){
    LOGICAL(returnvalue)[0] = FALSE;
    UNPROTECT(1);
    return returnvalue;
  }

  rows = asInteger(R_rows);
    
  if(!dbm_setRows(Matrix,rows)){
    LOGICAL(returnvalue)[0] = FALSE;
    UNPROTECT(1);
    return returnvalue;
  }   
  LOGICAL(returnvalue)[0] = TRUE;
  UNPROTECT(1);
  return returnvalue;

}


/*****************************************************
 **
 ** SEXP R_bm_AddColumn(SEXP R_BufferedMatrix)
 ** 
 ** SEXP R_BufferedMatrix
 **
 ** Adds an additional column to the matrix. Note the currently
 ** columns may only be added not removed
 **
 ** RETURNS pointer to the BufferedMatrix
 ** 
 **
 *****************************************************/

SEXP R_bm_AddColumn(SEXP R_BufferedMatrix){


  doubleBufferedMatrix Matrix;
    
  Matrix =  R_ExternalPtrAddr(R_BufferedMatrix);
  
  if (Matrix == NULL){
    return R_BufferedMatrix;
  }

  
  dbm_AddColumn(Matrix);

  return R_BufferedMatrix;
  

}

/*****************************************************
 **
 ** SEXP R_bm_ResizeBuffer(SEXP R_BufferedMatrix, SEXP R_new_maxrow, SEXP R_new_maxcol)
 ** 
 ** SEXP R_BufferedMatrix
 ** SEXP R_new_maxrow,R_new_maxcol - buffer dimensions
 **
 ** Resizes the buffers
 **
 ** RETURNS a pointer to the BufferedMatrix
 **
 *****************************************************/

SEXP R_bm_ResizeBuffer(SEXP R_BufferedMatrix, SEXP R_new_maxrow, SEXP R_new_maxcol){
  
  doubleBufferedMatrix Matrix;
  int new_maxrow, new_maxcol;

    
  Matrix =  R_ExternalPtrAddr(R_BufferedMatrix);
  
  if (Matrix == NULL){
    return R_BufferedMatrix;
  }

  new_maxrow = asInteger(R_new_maxrow);
  new_maxcol = asInteger(R_new_maxcol);

  dbm_ResizeBuffer(Matrix, new_maxrow, new_maxcol);
  return R_BufferedMatrix;
}

/*****************************************************
 **
 ** SEXP R_bm_RowMode(SEXP R_BufferedMatrix)
 **
 ** SEXP R_BufferedMatrix
 **
 ** turns the row buffer on
 **
 ** RETURNS a pointer to the BufferedMatrix
 **
 *****************************************************/

SEXP R_bm_RowMode(SEXP R_BufferedMatrix){
  
  doubleBufferedMatrix Matrix;
    
  Matrix =  R_ExternalPtrAddr(R_BufferedMatrix);
  
  if (Matrix == NULL){
    return R_BufferedMatrix;
  }
  dbm_RowMode(Matrix);
  
  return R_BufferedMatrix;

}


/*****************************************************
 **
 ** SEXP R_bm_ColMode(SEXP R_BufferedMatrix)
 **
 ** SEXP R_BufferedMatrix
 **
 ** turns the row buffer off
 **
 ** RETURNS a pointer to the BufferedMatrix
 **
 *****************************************************/

SEXP R_bm_ColMode(SEXP R_BufferedMatrix){
  
  doubleBufferedMatrix Matrix;
    
  Matrix =  R_ExternalPtrAddr(R_BufferedMatrix);
  
  if (Matrix == NULL){
    return R_BufferedMatrix;
  }
  dbm_ColMode(Matrix);
  
  return R_BufferedMatrix;

}

/*****************************************************
 **
 ** SEXP R_bm_SetPrefix(SEXP R_BufferedMatrix, SEXP R_Prefix)
 **
 ** SEXP R_BufferedMatrix
 ** SEXP R_Prefix - string to use for the prefix
 **
 ** Sets the prefix (ie string at the beginning) of any temporary 
 ** files created. Note that changing the prefix has no effect on
 ** temporary files already created
 **
 ** RETURNS a pointer to the BufferedMatrix
 **
 *****************************************************/


SEXP R_bm_SetPrefix(SEXP R_BufferedMatrix, SEXP R_Prefix){

  
  doubleBufferedMatrix Matrix;
  char *prefix = CHAR(VECTOR_ELT(R_Prefix,0));


  Matrix =  R_ExternalPtrAddr(R_BufferedMatrix);
  
  if (Matrix == NULL){
    return R_BufferedMatrix;
  }
  
  dbm_SetPrefix(Matrix,prefix);
  

  return R_BufferedMatrix;
}

/*****************************************************
 **
 ** SEXP R_bm_ReadOnlyModeToggle(SEXP R_BufferedMatrix)
 **
 ** SEXP R_BufferedMatrix
 **
 ** Changes the Mode from or to ReadOnly mode.
 ** It flips the state from one to the other
 **
 ** RETURNS a pointer to the BufferedMatrix
 **
 **
 *****************************************************/

SEXP R_bm_ReadOnlyModeToggle(SEXP R_BufferedMatrix){


  doubleBufferedMatrix Matrix;
  int current_mode;


  Matrix =  R_ExternalPtrAddr(R_BufferedMatrix);
  
  if (Matrix == NULL){
    return R_BufferedMatrix;
  }
  
  current_mode = dbm_isReadOnlyMode(Matrix);

  dbm_ReadOnlyMode(Matrix,!current_mode);
    
  return R_BufferedMatrix;
}

/*****************************************************
 **
 ** SEXP R_bm_isReadOnlyMode(SEXP R_BufferedMatrix)
 **
 ** SEXP R_BufferedMatrix
 **
 ** RETURNS TRUE if in ReadOnlyMode (ie can't set values)
 **         FALSE otherwise
 **
 **
 *****************************************************/



SEXP R_bm_isReadOnlyMode(SEXP R_BufferedMatrix){

  SEXP returnvalue;

  doubleBufferedMatrix Matrix;
  int current_mode;


  Matrix =  R_ExternalPtrAddr(R_BufferedMatrix);
  
  if (Matrix == NULL){
    PROTECT(returnvalue=allocVector(LGLSXP,1));
    LOGICAL(returnvalue)[0] = current_mode;
    UNPROTECT(1);
    return returnvalue;
  }
  
  current_mode = dbm_isReadOnlyMode(Matrix);
  
  

  PROTECT(returnvalue=allocVector(LGLSXP,1));

  LOGICAL(returnvalue)[0] = current_mode;
  UNPROTECT(1);
  return returnvalue;
}

/*****************************************************
 **
 ** SEXP R_bm_isRowMode(SEXP R_BufferedMatrix)
 ** 
 ** SEXP R_BufferedMatrix
 ** 
 ** RETURNS TRUE if in RowMode (ie RowBuffer has been activated)
 **         FALSE otherwise
 **
 *****************************************************/

SEXP R_bm_isRowMode(SEXP R_BufferedMatrix){

  SEXP returnvalue;

  doubleBufferedMatrix Matrix;
  int current_mode;


  Matrix =  R_ExternalPtrAddr(R_BufferedMatrix);
  
  if (Matrix == NULL){
    PROTECT(returnvalue=allocVector(LGLSXP,1));
    LOGICAL(returnvalue)[0] = FALSE;
    UNPROTECT(1);
    return returnvalue;
  }
  
  current_mode = dbm_isRowMode(Matrix);
  
  

  PROTECT(returnvalue=allocVector(LGLSXP,1));

  LOGICAL(returnvalue)[0] = current_mode;
  UNPROTECT(1);
  return returnvalue;
}

/*****************************************************
 **
 ** SEXP R_bm_getSize(SEXP R_BufferedMatrix)
 **
 ** SEXP R_BufferedMatrix
 **
 ** RETURNS the matrix dimensions as a vector of integers first
 **         element is rows, second element is columns
 **
 *****************************************************/

SEXP R_bm_getSize(SEXP R_BufferedMatrix){

  SEXP returnvalue;
  doubleBufferedMatrix Matrix;

  Matrix =  R_ExternalPtrAddr(R_BufferedMatrix);
  
  
  PROTECT(returnvalue=allocVector(INTSXP,2));

  if (Matrix == NULL){ 

    INTEGER(returnvalue)[0] = 0;
    INTEGER(returnvalue)[1] = 0;
    UNPROTECT(1);
    return returnvalue;
  }
  
  INTEGER(returnvalue)[0] = dbm_getRows(Matrix);
  INTEGER(returnvalue)[1] = dbm_getCols(Matrix);
  UNPROTECT(1);
  return returnvalue;


}

/*****************************************************
 **
 ** SEXP R_bm_getBufferSize(SEXP R_BufferedMatrix)
 **
 ** SEXP R_BufferedMatrix
 **
 ** RETURNS the buffer dimensions as a vector of integers first
 **         element is rows, second element is columns
 **
 *****************************************************/

SEXP R_bm_getBufferSize(SEXP R_BufferedMatrix){

  SEXP returnvalue;
  doubleBufferedMatrix Matrix;

  Matrix =  R_ExternalPtrAddr(R_BufferedMatrix);
  
  
  PROTECT(returnvalue=allocVector(INTSXP,2));

  if (Matrix == NULL){ 

    INTEGER(returnvalue)[0] = 0;
    INTEGER(returnvalue)[1] = 0;
    UNPROTECT(1);
    return returnvalue;
  }
  
  INTEGER(returnvalue)[0] = dbm_getBufferRows(Matrix);
  INTEGER(returnvalue)[1] = dbm_getBufferCols(Matrix);
  UNPROTECT(1);
  return returnvalue;


}

/*****************************************************
 **
 ** SEXP R_bm_getValue(SEXP R_BufferedMatrix, SEXP R_row, SEXP R_col)
 **
 ** SEXP R_BufferedMatrix
 ** SEXP R_row, R_col - location in the matrix
 **
 ** RETURNS value stored in BufferedMatrix at specified location
 **         Note that if a location outside the matrix dimensions is 
 **         Specified then NA is returned.
 ** 
 **
 *****************************************************/

SEXP R_bm_getValue(SEXP R_BufferedMatrix, SEXP R_row, SEXP R_col){

  
  SEXP returnvalue;
  doubleBufferedMatrix Matrix;

  Matrix =  R_ExternalPtrAddr(R_BufferedMatrix);
    
  PROTECT(returnvalue=allocVector(REALSXP,1));

  if (Matrix == NULL){ 
    REAL(returnvalue)[0] = R_NaReal;
    UNPROTECT(1); 
    return R_BufferedMatrix;
  }



  if(!dbm_getValue(Matrix,asInteger(R_row), asInteger(R_col), &REAL(returnvalue)[0])){
    REAL(returnvalue)[0] = R_NaReal;
  }

  UNPROTECT(1);
  return returnvalue;

}


/*****************************************************
 **
 ** SEXP R_bm_setValue(SEXP R_BufferedMatrix, SEXP R_row, SEXP R_col,SEXP value)
 **
 **
 ** SEXP R_BufferedMatrix
 ** SEXP R_row, R_col - location in the matrix
 ** SEXP value - store Numeric value at specified location
 **
 ** RETURNS TRUE if successful
 **         FALSE if unsuccessful
 ** 
 **
 *****************************************************/


SEXP R_bm_setValue(SEXP R_BufferedMatrix, SEXP R_row, SEXP R_col, SEXP value){
  
  doubleBufferedMatrix Matrix;
  SEXP returnvalue;
  
  PROTECT(returnvalue=allocVector(LGLSXP,1));


  Matrix =  R_ExternalPtrAddr(R_BufferedMatrix);
    
  if (Matrix == NULL){   
    LOGICAL(returnvalue)[0] = FALSE;
    UNPROTECT(1);
    return returnvalue;
  }



  if(!dbm_setValue(Matrix,asInteger(R_row), asInteger(R_col), REAL(value)[0])){
    LOGICAL(returnvalue)[0] = FALSE;
    UNPROTECT(1);
    return returnvalue;
  }
  
  LOGICAL(returnvalue)[0] = TRUE;
  UNPROTECT(1);
  return returnvalue;

}







/*****************************************************
 **
 ** SEXP R_bm_getValueColumn(SEXP R_BufferedMatrix, SEXP R_col)
 **
 ** SEXP R_BufferedMatrix
 ** SEXP R_col - Columns to access in the matrix
 **
 ** RETURNS values stored in BufferedMatrix at specified location
 **         Note that if a location outside the matrix dimensions is 
 **         Specified then NA is returned.
 ** 
 **
 *****************************************************/

SEXP R_bm_getValueColumn(SEXP R_BufferedMatrix, SEXP R_col){


  
  SEXP returnvalue;
  doubleBufferedMatrix Matrix;
  int i,j;
  int ncols;


  Matrix =  R_ExternalPtrAddr(R_BufferedMatrix);

  ncols = length(R_col);
    
  PROTECT(returnvalue=allocMatrix(REALSXP,dbm_getRows(Matrix),ncols));

  if (Matrix == NULL){ 
    for (i=0; i < dbm_getRows(Matrix)*ncols; i++){
      REAL(returnvalue)[i] = R_NaReal;
    }
    UNPROTECT(1); 
    return returnvalue;
  }

	  
  if(!dbm_getValueColumn(Matrix, INTEGER(R_col), REAL(returnvalue),ncols)){
    for (j = 0; j < ncols; j++){
      for (i=0; i < dbm_getRows(Matrix); i++){
	REAL(returnvalue)[j*dbm_getRows(Matrix) + i] = R_NaReal;
      }
    }
  }
  
  UNPROTECT(1);
  return returnvalue;


}



/*****************************************************
 **
 ** SEXP R_bm_getValueRow(SEXP R_BufferedMatrix, SEXP R_row)
 **
 ** SEXP R_BufferedMatrix
 ** SEXP R_row - Rows to access in the matrix
 **
 ** RETURNS values stored in BufferedMatrix at specified location
 **         Note that if a location outside the matrix dimensions is 
 **         Specified then NA is returned.
 ** 
 **
 *****************************************************/

SEXP R_bm_getValueRow(SEXP R_BufferedMatrix, SEXP R_row){
  
  SEXP returnvalue;
  doubleBufferedMatrix Matrix;
  int i,j;
  int nrows;

  Matrix =  R_ExternalPtrAddr(R_BufferedMatrix);

  nrows = length(R_row);
  

  PROTECT(returnvalue=allocMatrix(REALSXP,nrows,dbm_getCols(Matrix)));

  if (Matrix == NULL){ 
    for (i=0; i < dbm_getCols(Matrix)*nrows; i++){
      REAL(returnvalue)[i] = R_NaReal;
    }
    UNPROTECT(1); 
    return returnvalue;
  }
  

 
  if(!dbm_getValueRow(Matrix, INTEGER(R_row), REAL(returnvalue), nrows)){
    for (i = 0; i < nrows; i++){
      for (j=0; j < dbm_getCols(Matrix); j++){
	REAL(returnvalue)[j*nrows + i] = R_NaReal;
      }
    } 
  }


  UNPROTECT(1);
  return returnvalue;


}




/*****************************************************
 **
 ** SEXP R_bm_getValueSubmatrix(SEXP R_BufferedMatrix, SEXP R_row, SEXP R_col)
 **
 ** SEXP R_BufferedMatrix
 ** SEXP R_row - Rows to access in the matrix
 ** SEXP R_col - Columns to access in the matrix
 **
 ** RETURNS values stored in BufferedMatrix at specified locations
 **         Note that if a location outside the matrix dimensions is 
 **         Specified then NA is returned.
 ** 
 ** This function gets specified section of the matrix as specified
 ** by rows and columns
 **
 *****************************************************/


SEXP R_bm_getValueSubmatrix(SEXP R_BufferedMatrix, SEXP R_row, SEXP R_col){
  

  SEXP returnvalue;
  doubleBufferedMatrix Matrix;
  int i,j;
  int nrows,ncols;

  double tempbuffer;


  Matrix =  R_ExternalPtrAddr(R_BufferedMatrix);

  nrows = length(R_row);
  ncols = length(R_col);
  

  PROTECT(returnvalue=allocMatrix(REALSXP,nrows,ncols));

  if (Matrix == NULL){ 
    for (i=0; i < ncols*nrows; i++){
      REAL(returnvalue)[i] = R_NaReal;
    }
    UNPROTECT(1); 
    return returnvalue;
  }
  
  for (j=0; j < ncols; j++){
    for (i = 0; i < nrows; i++){
      if(!dbm_getValue(Matrix,INTEGER(R_row)[i],  INTEGER(R_col)[j], &REAL(returnvalue)[j*nrows + i])){
	REAL(returnvalue)[j*nrows + i] = R_NaReal;
      }
    }
  }
  UNPROTECT(1); 
  return returnvalue;

}











/*****************************************************
 **
 ** SEXP R_bm_setValueColumn(SEXP R_BufferedMatrix, SEXP R_col, SEXP value)
 **
 ** SEXP R_BufferedMatrix
 ** SEXP R_col - Columns to access in the matrix
 ** SEXP value - store Numeric values at specified locations
 **
 ** RETURNS TRUE if successful
 **         FALSE if unsuccessful
 **
 ** 
 **
 *****************************************************/

SEXP R_bm_setValueColumn(SEXP R_BufferedMatrix, SEXP R_col, SEXP value){


  
  SEXP returnvalue;
  doubleBufferedMatrix Matrix;
  int i,j;
  int ncols;


  Matrix =  R_ExternalPtrAddr(R_BufferedMatrix);

  ncols = length(R_col);
    
  PROTECT(returnvalue=allocVector(LGLSXP,1));

  if (Matrix == NULL){ 
    LOGICAL(returnvalue)[0] = FALSE;
    UNPROTECT(1);
    return returnvalue;
  }

	  
  if(!dbm_setValueColumn(Matrix, INTEGER(R_col), REAL(value), ncols)){ 
    LOGICAL(returnvalue)[0] = FALSE;
    UNPROTECT(1);
    return returnvalue;
  } 


  LOGICAL(returnvalue)[0] = TRUE;
  UNPROTECT(1);
  return returnvalue;


}






SEXP R_bm_setValueRow(SEXP R_BufferedMatrix, SEXP R_row, SEXP value){


  
  SEXP returnvalue;
  doubleBufferedMatrix Matrix;
  int i,j;
  int nrows;
  double *tempbuffer;
    

  Matrix =  R_ExternalPtrAddr(R_BufferedMatrix);

  nrows = length(R_row);
    
  PROTECT(returnvalue=allocVector(LGLSXP,1));

  if (Matrix == NULL){ 
    LOGICAL(returnvalue)[0] = FALSE;
    UNPROTECT(1);
    return returnvalue;
  }

  
  if(!dbm_setValueRow(Matrix, INTEGER(R_row), REAL(value),nrows)){
    LOGICAL(returnvalue)[0] = FALSE;
    UNPROTECT(1);
    return returnvalue;
  }

  LOGICAL(returnvalue)[0] = TRUE;
  UNPROTECT(1);
  return returnvalue;


}



SEXP R_bm_setValueSubmatrix(SEXP R_BufferedMatrix, SEXP R_row, SEXP R_col, SEXP value){



  SEXP returnvalue;
  doubleBufferedMatrix Matrix;
  int i,j;
  int nrows,ncols;

  
  Matrix =  R_ExternalPtrAddr(R_BufferedMatrix);

  nrows = length(R_row);
  ncols = length(R_col);
  
  PROTECT(returnvalue=allocVector(LGLSXP,1));
  if (Matrix == NULL){ 
    LOGICAL(returnvalue)[0] = FALSE;
    UNPROTECT(1);
    return returnvalue;
  }


  for (j=0; j < ncols; j++){
    for (i = 0; i < nrows; i++){
      if(!dbm_setValue(Matrix,INTEGER(R_row)[i],  INTEGER(R_col)[j], REAL(value)[j*nrows + i])){
	
	LOGICAL(returnvalue)[0] = FALSE;
	UNPROTECT(1);
	return returnvalue;
      }
    }
  }


  
  LOGICAL(returnvalue)[0] = TRUE;
  UNPROTECT(1);
  return returnvalue;


}




SEXP R_bm_getPrefix(SEXP R_BufferedMatrix){


  SEXP returnvalue;
  doubleBufferedMatrix Matrix;

  char *prefix;

  
  Matrix =  R_ExternalPtrAddr(R_BufferedMatrix);
  
  if (Matrix == NULL){
    return R_BufferedMatrix;
  }


  prefix = dbm_getPrefix(Matrix);

  PROTECT(returnvalue = allocVector(STRSXP,1));

  SET_VECTOR_ELT(returnvalue,0,mkChar(prefix));

  


  Free(prefix);
  UNPROTECT(1);
  return returnvalue;

}





SEXP R_bm_getDirectory(SEXP R_BufferedMatrix){


  SEXP returnvalue;
  doubleBufferedMatrix Matrix;

  char *directory;

  
  Matrix =  R_ExternalPtrAddr(R_BufferedMatrix);
  
  if (Matrix == NULL){
    return R_BufferedMatrix;
  }


  directory = dbm_getDirectory(Matrix);

  PROTECT(returnvalue = allocVector(STRSXP,1));

  SET_VECTOR_ELT(returnvalue,0,mkChar(directory));

  


  Free(directory);
  UNPROTECT(1);
  return returnvalue;

}







SEXP R_bm_copyValues(SEXP R_BufferedMatrix_target, SEXP R_BufferedMatrix_source){


  SEXP returnvalue;
  doubleBufferedMatrix Matrix_target;
  doubleBufferedMatrix Matrix_source;

  char *directory;

  
  Matrix_target =  R_ExternalPtrAddr(R_BufferedMatrix_target);
  Matrix_source =  R_ExternalPtrAddr(R_BufferedMatrix_source);

  /* Check the two supplied BufferedMatrices */
  if (Matrix_target == NULL){
    error("Non valid BufferedMatrix supplied as target\n");
  }
  
  if (Matrix_source == NULL){
    error("Non valid BufferedMatrix supplied as source\n");
  }
  
  if ((dbm_getRows(Matrix_source) != dbm_getRows(Matrix_target)) || (dbm_getCols(Matrix_source) != dbm_getCols(Matrix_target))){
    error("Matrices sizes do not agree. Source dimensions: %d %d Target dimensions: %d %d\n",dbm_getRows(Matrix_source),dbm_getCols(Matrix_source),dbm_getRows(Matrix_target),dbm_getCols(Matrix_target));
  }
  
  
  PROTECT(returnvalue = allocVector(LGLSXP,1));

  
  
  if(!dbm_copyValues(Matrix_target,Matrix_source)){
    LOGICAL(returnvalue)[0] = FALSE;
    UNPROTECT(1);
    return returnvalue;
  }

  LOGICAL(returnvalue)[0] = TRUE;
  UNPROTECT(1);
  return returnvalue;


}




static SEXP Rfn_eval(SEXP x, SEXP f, SEXP rho)
{
  defineVar(install("x"), x, rho);
  return(eval(f, rho));
}






SEXP R_bm_ewApply(SEXP R_BufferedMatrix, SEXP Rfn, SEXP rho){


  SEXP temp;
  SEXP returnvalue;

  doubleBufferedMatrix Matrix;
  int i,j;


    
  Matrix =  R_ExternalPtrAddr(R_BufferedMatrix);

  /* Check the two supplied BufferedMatrices */
  if (Matrix == NULL){
    error("Non valid BufferedMatrix supplied.\n");
  }
  

  
  PROTECT(temp=allocMatrix(REALSXP,dbm_getRows(Matrix),1));
  PROTECT(returnvalue = allocVector(LGLSXP,1));
  

  for (j=0; j < dbm_getCols(Matrix); j++){
    if(!dbm_getValueColumn(Matrix, &j, REAL(temp),1)){
      LOGICAL(returnvalue)[0] = FALSE;
      UNPROTECT(2);
      return returnvalue;
    }
    temp = Rfn_eval(temp,Rfn,rho);
    if(!dbm_setValueColumn(Matrix, &j, REAL(temp), 1)){ 
      LOGICAL(returnvalue)[0] = FALSE;
      UNPROTECT(2);
      return returnvalue;
    } 
    
  }
  




  LOGICAL(returnvalue)[0] = TRUE;
  UNPROTECT(2);
  return returnvalue;







}




static double bm_log(double x, double *param){  
  return(log(x)/log(param[0]));
}


static double bm_pow(double x, double *param){
  return(pow(x,param[0]));
}


static double bm_sqrt(double x, double *param){
  return(sqrt(x));
}



static double bm_exp(double x, double *param){
  return(exp(x));
}




SEXP R_bm_ewSqrt(SEXP R_BufferedMatrix){
  
  
  doubleBufferedMatrix Matrix;
  double *param=0;
    
  Matrix =  R_ExternalPtrAddr(R_BufferedMatrix);

  /* Check the supplied BufferedMatrices */
  if (Matrix == NULL){
    error("Non valid BufferedMatrix supplied.\n");
  }
  
  
  dbm_ewApply(Matrix,&bm_sqrt,param);

  
  
  return R_BufferedMatrix;
}


SEXP R_bm_ewExp(SEXP R_BufferedMatrix){
  
  
  doubleBufferedMatrix Matrix;
  double *param=0;
    
  Matrix =  R_ExternalPtrAddr(R_BufferedMatrix);

  /* Check the supplied BufferedMatrices */
  if (Matrix == NULL){
    error("Non valid BufferedMatrix supplied.\n");
  }
  
  
  dbm_ewApply(Matrix,&bm_exp,param);

  
  
  return R_BufferedMatrix;
}









SEXP R_bm_ewPow(SEXP R_BufferedMatrix,SEXP power){
  
  
  doubleBufferedMatrix Matrix;
  double param=0;
    
  Matrix =  R_ExternalPtrAddr(R_BufferedMatrix);

  /* Check the supplied BufferedMatrices */
  if (Matrix == NULL){
    error("Non valid BufferedMatrix supplied.\n");
  }
  
  
  param = REAL(power)[0];


  dbm_ewApply(Matrix,&bm_pow,&param);

  
  
  return R_BufferedMatrix;
}


SEXP R_bm_ewLog(SEXP R_BufferedMatrix,SEXP base){
  
  
  doubleBufferedMatrix Matrix;
  double param=0;
    
  Matrix =  R_ExternalPtrAddr(R_BufferedMatrix);

  /* Check the supplied BufferedMatrices */
  if (Matrix == NULL){
    error("Non valid BufferedMatrix supplied.\n");
  }
  
  
  param = REAL(base)[0];


  dbm_ewApply(Matrix,&bm_log,&param);

  
  
  return R_BufferedMatrix;
}







SEXP R_bm_max(SEXP R_BufferedMatrix,SEXP removeNA){

  SEXP returnvalue;
  doubleBufferedMatrix Matrix;
  int NAflag;

  int foundfinite;

  Matrix =  R_ExternalPtrAddr(R_BufferedMatrix);

  /* Check the supplied BufferedMatrices */
  if (Matrix == NULL){
    error("Non valid BufferedMatrix supplied.\n");
  }
  
  PROTECT(returnvalue = allocVector(REALSXP,1));
  NAflag = LOGICAL(removeNA)[0];

  REAL(returnvalue)[0] = dbm_max(Matrix,NAflag,&foundfinite);

  if (!foundfinite && NAflag){
    warning("No finite arguments to max; returning -Inf");
  }


  UNPROTECT(1);
  return returnvalue;

}





SEXP R_bm_min(SEXP R_BufferedMatrix,SEXP removeNA){

  SEXP returnvalue;
  doubleBufferedMatrix Matrix;
  int NAflag;
  int foundfinite;

  Matrix =  R_ExternalPtrAddr(R_BufferedMatrix);

  /* Check the supplied BufferedMatrices */
  if (Matrix == NULL){
    error("Non valid BufferedMatrix supplied.\n");
  }
  
  PROTECT(returnvalue = allocVector(REALSXP,1));
  NAflag = LOGICAL(removeNA)[0];

  REAL(returnvalue)[0] = dbm_min(Matrix,NAflag, &foundfinite);

  if (!foundfinite && NAflag){
    warning("No finite arguments to Min; returning Inf");
  }
    



  UNPROTECT(1);
  return returnvalue;

}



SEXP R_bm_mean(SEXP R_BufferedMatrix,SEXP removeNA){

  SEXP returnvalue;
  doubleBufferedMatrix Matrix;
  int NAflag;


  Matrix =  R_ExternalPtrAddr(R_BufferedMatrix);

  /* Check the supplied BufferedMatrices */
  if (Matrix == NULL){
    error("Non valid BufferedMatrix supplied.\n");
  }
  
  PROTECT(returnvalue = allocVector(REALSXP,1));
  NAflag = LOGICAL(removeNA)[0];

  REAL(returnvalue)[0] = dbm_mean(Matrix,NAflag);


  UNPROTECT(1);
  return returnvalue;

}


SEXP R_bm_sum(SEXP R_BufferedMatrix,SEXP removeNA){

  SEXP returnvalue;
  doubleBufferedMatrix Matrix;
  int NAflag;


  Matrix =  R_ExternalPtrAddr(R_BufferedMatrix);

  /* Check the supplied BufferedMatrices */
  if (Matrix == NULL){
    error("Non valid BufferedMatrix supplied.\n");
  }
  
  PROTECT(returnvalue = allocVector(REALSXP,1));
  NAflag = LOGICAL(removeNA)[0];

  REAL(returnvalue)[0] = dbm_sum(Matrix,NAflag);


  UNPROTECT(1);
  return returnvalue;

}


SEXP R_bm_var(SEXP R_BufferedMatrix,SEXP removeNA){

  SEXP returnvalue;
  doubleBufferedMatrix Matrix;
  int NAflag;


  Matrix =  R_ExternalPtrAddr(R_BufferedMatrix);

  /* Check the supplied BufferedMatrices */
  if (Matrix == NULL){
    error("Non valid BufferedMatrix supplied.\n");
  }
  
  PROTECT(returnvalue = allocVector(REALSXP,1));
  NAflag = LOGICAL(removeNA)[0];

  REAL(returnvalue)[0] = dbm_var(Matrix,NAflag);


  UNPROTECT(1);
  return returnvalue;

}








SEXP R_bm_rowMeans(SEXP R_BufferedMatrix,SEXP removeNA){

  SEXP returnvalue;
  doubleBufferedMatrix Matrix;
  int NAflag;


  Matrix =  R_ExternalPtrAddr(R_BufferedMatrix);

  /* Check the supplied BufferedMatrices */
  if (Matrix == NULL){
    error("Non valid BufferedMatrix supplied.\n");
  }
  
  PROTECT(returnvalue = allocVector(REALSXP,dbm_getRows(Matrix)));
  NAflag = LOGICAL(removeNA)[0];

  dbm_rowMeans(Matrix,NAflag,REAL(returnvalue));


  UNPROTECT(1);
  return returnvalue;

}






SEXP R_bm_rowSums(SEXP R_BufferedMatrix,SEXP removeNA){

  SEXP returnvalue;
  doubleBufferedMatrix Matrix;
  int NAflag;


  Matrix =  R_ExternalPtrAddr(R_BufferedMatrix);

  /* Check the supplied BufferedMatrices */
  if (Matrix == NULL){
    error("Non valid BufferedMatrix supplied.\n");
  }
  
  PROTECT(returnvalue = allocVector(REALSXP,dbm_getRows(Matrix)));
  NAflag = LOGICAL(removeNA)[0];

  dbm_rowSums(Matrix,NAflag,REAL(returnvalue));


  UNPROTECT(1);
  return returnvalue;

}


SEXP R_bm_rowVars(SEXP R_BufferedMatrix,SEXP removeNA){

  SEXP returnvalue;
  doubleBufferedMatrix Matrix;
  int NAflag;


  Matrix =  R_ExternalPtrAddr(R_BufferedMatrix);

  /* Check the supplied BufferedMatrices */
  if (Matrix == NULL){
    error("Non valid BufferedMatrix supplied.\n");
  }
  
  PROTECT(returnvalue = allocVector(REALSXP,dbm_getRows(Matrix)));
  NAflag = LOGICAL(removeNA)[0];

  dbm_rowVars(Matrix,NAflag,REAL(returnvalue));


  UNPROTECT(1);
  return returnvalue;

}


SEXP R_bm_rowMax(SEXP R_BufferedMatrix,SEXP removeNA){

  SEXP returnvalue;
  doubleBufferedMatrix Matrix;
  int NAflag;


  Matrix =  R_ExternalPtrAddr(R_BufferedMatrix);

  /* Check the supplied BufferedMatrices */
  if (Matrix == NULL){
    error("Non valid BufferedMatrix supplied.\n");
  }
  
  PROTECT(returnvalue = allocVector(REALSXP,dbm_getRows(Matrix)));
  NAflag = LOGICAL(removeNA)[0];

  dbm_rowMax(Matrix,NAflag,REAL(returnvalue));


  UNPROTECT(1);
  return returnvalue;

}



SEXP R_bm_rowMin(SEXP R_BufferedMatrix,SEXP removeNA){

  SEXP returnvalue;
  doubleBufferedMatrix Matrix;
  int NAflag;


  Matrix =  R_ExternalPtrAddr(R_BufferedMatrix);

  /* Check the supplied BufferedMatrices */
  if (Matrix == NULL){
    error("Non valid BufferedMatrix supplied.\n");
  }
  
  PROTECT(returnvalue = allocVector(REALSXP,dbm_getRows(Matrix)));
  NAflag = LOGICAL(removeNA)[0];

  dbm_rowMin(Matrix,NAflag,REAL(returnvalue));


  UNPROTECT(1);
  return returnvalue;

}



SEXP R_bm_colMeans(SEXP R_BufferedMatrix,SEXP removeNA){

  SEXP returnvalue;
  doubleBufferedMatrix Matrix;
  int NAflag;


  Matrix =  R_ExternalPtrAddr(R_BufferedMatrix);

  /* Check the supplied BufferedMatrices */
  if (Matrix == NULL){
    error("Non valid BufferedMatrix supplied.\n");
  }
  
  PROTECT(returnvalue = allocVector(REALSXP,dbm_getCols(Matrix)));
  NAflag = LOGICAL(removeNA)[0];

  dbm_colMeans(Matrix,NAflag,REAL(returnvalue));


  UNPROTECT(1);
  return returnvalue;

}



SEXP R_bm_colSums(SEXP R_BufferedMatrix,SEXP removeNA){

  SEXP returnvalue;
  doubleBufferedMatrix Matrix;
  int NAflag;


  Matrix =  R_ExternalPtrAddr(R_BufferedMatrix);

  /* Check the supplied BufferedMatrices */
  if (Matrix == NULL){
    error("Non valid BufferedMatrix supplied.\n");
  }
  
  PROTECT(returnvalue = allocVector(REALSXP,dbm_getCols(Matrix)));
  NAflag = LOGICAL(removeNA)[0];

  dbm_colSums(Matrix,NAflag,REAL(returnvalue));


  UNPROTECT(1);
  return returnvalue;

}



SEXP R_bm_colVars(SEXP R_BufferedMatrix,SEXP removeNA){

  SEXP returnvalue;
  doubleBufferedMatrix Matrix;
  int NAflag;


  Matrix =  R_ExternalPtrAddr(R_BufferedMatrix);

  /* Check the supplied BufferedMatrices */
  if (Matrix == NULL){
    error("Non valid BufferedMatrix supplied.\n");
  }
  
  PROTECT(returnvalue = allocVector(REALSXP,dbm_getCols(Matrix)));
  NAflag = LOGICAL(removeNA)[0];

  dbm_colVars(Matrix,NAflag,REAL(returnvalue));


  UNPROTECT(1);
  return returnvalue;

}



SEXP R_bm_colMax(SEXP R_BufferedMatrix,SEXP removeNA){

  SEXP returnvalue;
  doubleBufferedMatrix Matrix;
  int NAflag;


  Matrix =  R_ExternalPtrAddr(R_BufferedMatrix);

  /* Check the supplied BufferedMatrices */
  if (Matrix == NULL){
    error("Non valid BufferedMatrix supplied.\n");
  }
  
  PROTECT(returnvalue = allocVector(REALSXP,dbm_getCols(Matrix)));
  NAflag = LOGICAL(removeNA)[0];

  dbm_colMax(Matrix,NAflag,REAL(returnvalue));


  UNPROTECT(1);
  return returnvalue;

}



SEXP R_bm_colMin(SEXP R_BufferedMatrix,SEXP removeNA){

  SEXP returnvalue;
  doubleBufferedMatrix Matrix;
  int NAflag;


  Matrix =  R_ExternalPtrAddr(R_BufferedMatrix);

  /* Check the supplied BufferedMatrices */
  if (Matrix == NULL){
    error("Non valid BufferedMatrix supplied.\n");
  }
  
  PROTECT(returnvalue = allocVector(REALSXP,dbm_getCols(Matrix)));
  NAflag = LOGICAL(removeNA)[0];

  dbm_colMin(Matrix,NAflag,REAL(returnvalue));


  UNPROTECT(1);
  return returnvalue;

}

