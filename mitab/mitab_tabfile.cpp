/**********************************************************************
 * $Id: mitab_tabfile.cpp,v 1.4 1999-09-16 02:39:17 daniel Exp $
 *
 * Name:     mitab_tabfile.cpp
 * Project:  MapInfo TAB Read/Write library
 * Language: C++
 * Purpose:  Implementation of the TABFile class, the main class of the lib.
 *           To be used by external programs to handle reading/writing of
 *           features from/to TAB datasets.
 * Author:   Daniel Morissette, danmo@videotron.ca
 *
 **********************************************************************
 * Copyright (c) 1999, Daniel Morissette
 *
 * All rights reserved.  This software may be copied or reproduced, in
 * all or in part, without the prior written consent of its author,
 * Daniel Morissette (danmo@videotron.ca).  However, any material copied
 * or reproduced must bear the original copyright notice (above), this 
 * original paragraph, and the original disclaimer (below).
 * 
 * The entire risk as to the results and performance of the software,
 * supporting text and other information contained in this file
 * (collectively called the "Software") is with the user.  Although 
 * considerable efforts have been used in preparing the Software, the 
 * author does not warrant the accuracy or completeness of the Software.
 * In no event will the author be liable for damages, including loss of
 * profits or consequential damages, arising out of the use of the 
 * Software.
 * 
 **********************************************************************
 *
 * $Log: mitab_tabfile.cpp,v $
 * Revision 1.4  1999-09-16 02:39:17  daniel
 * Completed read support for most feature types
 *
 * Revision 1.3  1999/09/01 17:50:28  daniel
 * Added GetNativeFieldType() and GetFeatureDefn()
 *
 * Revision 1.2  1999/07/14 05:20:42  warmerda
 * added first pass of projection creation
 *
 * Revision 1.1  1999/07/12 04:18:25  daniel
 * Initial checkin
 *
 **********************************************************************/

#include "mitab.h"
#include "mitab_utils.h"


/*=====================================================================
 *                      class TABFile
 *====================================================================*/


/**********************************************************************
 *                   TABFile::TABFile()
 *
 * Constructor.
 **********************************************************************/
TABFile::TABFile()
{
    m_pszFname = NULL;
    m_papszTABFile = NULL;
    m_pszVersion = NULL;
    m_pszCharset = NULL;

    m_poMAPFile = NULL;
    m_poDATFile = NULL;
    m_poDefn = NULL;
    m_poSpatialRef = NULL;
    m_poCurFeature = NULL;
    m_nCurFeatureId = -1;
    m_nLastFeatureId = -1;

}

/**********************************************************************
 *                   TABFile::~TABFile()
 *
 * Destructor.
 **********************************************************************/
TABFile::~TABFile()
{
    Close();
}

/**********************************************************************
 *                   TABFile::Open()
 *
 * Open a .TAB dataset and the associated files, and initialize the 
 * structures to be ready to read features from it.
 *
 * Returns 0 on success, -1 on error.
 **********************************************************************/
int TABFile::Open(const char *pszFname, const char *pszAccess)
{
    char *pszTmpFname = NULL;
    int nFnameLen = 0;
    
    if (m_poMAPFile)
    {
        CPLError(CE_Failure, CPLE_FileIO,
                 "Open() failed: object already contains an open file");
        return -1;
    }

    /*-----------------------------------------------------------------
     * Validate access mode
     *----------------------------------------------------------------*/
    if (EQUALN(pszAccess, "r", 1))
    {
        m_eAccessMode = TABRead;
    }
    else if (EQUALN(pszAccess, "w", 1))
    {
        CPLError(CE_Failure, CPLE_NotSupported,
                 "Open() failed: write access not implemented yet!");
        return -1;

        m_eAccessMode = TABWrite;
    }
    else
    {
        CPLError(CE_Failure, CPLE_FileIO,
                 "Open() failed: access mode \"%s\" not supported", pszAccess);
        return -1;
    }

    /*-----------------------------------------------------------------
     * Make sure filename has a .TAB extension... 
     *----------------------------------------------------------------*/
    m_pszFname = CPLStrdup(pszFname);
    nFnameLen = strlen(m_pszFname);
    if (nFnameLen > 4 && (strcmp(m_pszFname+nFnameLen-4, ".TAB")==0 ||
                     strcmp(m_pszFname+nFnameLen-4, ".MAP")==0 ||
                     strcmp(m_pszFname+nFnameLen-4, ".DAT")==0 ) )
        strcpy(m_pszFname+nFnameLen-4, ".TAB");
    else if (nFnameLen > 4 && (strcmp(m_pszFname+nFnameLen-4, ".tab")==0 ||
                          strcmp(m_pszFname+nFnameLen-4, ".map")==0 ||
                          strcmp(m_pszFname+nFnameLen-4, ".dat")==0 ) )
        strcpy(m_pszFname+nFnameLen-4, ".tab");
    else
    {
        CPLError(CE_Failure, CPLE_FileIO,
                 "Open() failed for %s: invalid filename extension",
                 m_pszFname);
        CPLFree(m_pszFname);
        return -1;
    }

    pszTmpFname = CPLStrdup(m_pszFname);


#ifndef _WIN32
    /*-----------------------------------------------------------------
     * On Unix, make sure extension uses the right cases
     *----------------------------------------------------------------*/
    TABAdjustFilenameExtension(m_pszFname);
#endif

    /*-----------------------------------------------------------------
     * Open .TAB file... since it's a small text file, we will just load
     * it as a stringlist in memory.
     *----------------------------------------------------------------*/
    m_papszTABFile = CSLLoad(m_pszFname);
    if (m_papszTABFile == NULL)
    {
        // Failed... an error has already been produced.
        CPLFree(m_pszFname);
        return -1;
    }

    /*-----------------------------------------------------------------
     * Open .MAP file
     *----------------------------------------------------------------*/
    if (nFnameLen > 4 && strcmp(pszTmpFname+nFnameLen-4, ".TAB")==0)
        strcpy(pszTmpFname+nFnameLen-4, ".MAP");
    else 
        strcpy(pszTmpFname+nFnameLen-4, ".map");

#ifndef _WIN32
    TABAdjustFilenameExtension(pszTmpFname);
#endif

    m_poMAPFile = new TABMAPFile;
    m_poMAPFile->Open(pszTmpFname, pszAccess);

    if (m_poMAPFile == NULL)
    {
        // Open Failed... an error has already been reported, just return.
        CPLFree(pszTmpFname);
        Close();
        return -1;
    }

    m_nLastFeatureId = m_poMAPFile->GetMaxObjId();

    /*-----------------------------------------------------------------
     * Open .DAT file
     *----------------------------------------------------------------*/
    if (nFnameLen > 4 && strcmp(pszTmpFname+nFnameLen-4, ".MAP")==0)
        strcpy(pszTmpFname+nFnameLen-4, ".DAT");
    else 
        strcpy(pszTmpFname+nFnameLen-4, ".dat");

#ifndef _WIN32
    TABAdjustFilenameExtension(pszTmpFname);
#endif

    m_poDATFile = new TABDATFile;
    m_poDATFile->Open(pszTmpFname, pszAccess);


    if (m_poDATFile == NULL)
    {
        // Open Failed... an error has already been reported, just return.
        CPLFree(pszTmpFname);
        Close();
        return -1;
    }

    CPLFree(pszTmpFname);
    pszTmpFname = NULL;

    /*-----------------------------------------------------------------
     * Build FeatureDefn
     *----------------------------------------------------------------*/
    if (ParseTABFile() != 0)
    {
        // Failed... an error has already been reported, just return.
        CPLFree(pszTmpFname);
        Close();
        return -1;
    }

    /*-----------------------------------------------------------------
     * __TODO__ we could probably call GetSpatialRef() here to force
     * parsing the projection information... this would allow us to 
     * assignSpatialReference() on the geometries that we return.
     *----------------------------------------------------------------*/

    return 0;
}


/**********************************************************************
 *                   TABFile::ParseTABFile()
 *
 * Scan the lines of the TAB file, and store any useful information into
 * class members.  The main piece of information being the fields 
 * definition that we use to build the OGRFeatureDefn for this file.
 *
 * This private method should be used only during the Open() call.
 *
 * Returns 0 on success, -1 on error.
 **********************************************************************/
int TABFile::ParseTABFile()
{
    int         iLine, numLines, numTok, nStatus;
    char        **papszTok=NULL;
    GBool       bInsideTableDef = FALSE;
    OGRFieldDefn *poFieldDefn;

    m_poDefn = new OGRFeatureDefn("TABFeature");

    numLines = CSLCount(m_papszTABFile);

    for(iLine=0; iLine<numLines; iLine++)
    {
        /*-------------------------------------------------------------
         * Tokenize the next .TAB line, and check first keyword
         *------------------------------------------------------------*/
        CSLDestroy(papszTok);
        papszTok = CSLTokenizeStringComplex(m_papszTABFile[iLine], " \t(),;",
                                            TRUE, FALSE);
        if (CSLCount(papszTok) < 2)
            continue;   // All interesting lines have at least 2 tokens

        if (EQUAL(papszTok[0], "!version"))
        {
            m_pszVersion = CPLStrdup(papszTok[1]);
        }
        else if (EQUAL(papszTok[0], "!charset"))
        {
            m_pszCharset = CPLStrdup(papszTok[1]);
        }
        else if (EQUAL(papszTok[0], "Definition") &&
                 EQUAL(papszTok[1], "Table") )
        {
            bInsideTableDef = TRUE;
        }
        else if (bInsideTableDef &&
                 EQUAL(papszTok[0], "Fields"))
        {
            /*---------------------------------------------------------
             * We found the list of table fields
             *--------------------------------------------------------*/
            int iField, numFields;
            numFields = atoi(papszTok[1]);
            if (numFields < 1 || iLine+numFields >= numLines)
            {
                CPLError(CE_Failure, CPLE_FileIO,
                         "Invalid number of fields (%s) at line %d in file %s",
                         papszTok[1], iLine+1, m_pszFname);
                CSLDestroy(papszTok);
                return -1;
            }

            iLine++;
            poFieldDefn = NULL;
            for(iField=0; iField<numFields; iField++, iLine++)
            {
                /*-----------------------------------------------------
                 * For each field definition found in the .TAB:
                 * Pass the info to the DAT file object.  It will validate
                 * the info with what is found in the .DAT header, and will
                 * also use this info later to interpret field values.
                 *
                 * We also create the OGRFieldDefn at the same time to 
                 * initialize the OGRFeatureDefn
                 *----------------------------------------------------*/
                CSLDestroy(papszTok);
                papszTok = CSLTokenizeStringComplex(m_papszTABFile[iLine], 
                                                    " \t(),;",
                                                    TRUE, FALSE);
                numTok = CSLCount(papszTok);
                nStatus = -1;
                CPLAssert(m_poDefn);
                if (numTok >= 3 && EQUAL(papszTok[1], "char"))
                {
                    /*-------------------------------------------------
                     * CHAR type
                     *------------------------------------------------*/
                    nStatus = m_poDATFile->ValidateFieldInfoFromTAB(iField, 
                                                               papszTok[0],
                                                               TABFChar,
                                                            atoi(papszTok[2]),
                                                               0);
                    poFieldDefn = new OGRFieldDefn(papszTok[0], OFTString);
                    poFieldDefn->SetWidth(atoi(papszTok[2]));
                }
                else if (numTok >= 2 && EQUAL(papszTok[1], "integer"))
                {
                    /*-------------------------------------------------
                     * INTEGER type
                     *------------------------------------------------*/
                    nStatus = m_poDATFile->ValidateFieldInfoFromTAB(iField, 
                                                               papszTok[0],
                                                               TABFInteger,
                                                               0,
                                                               0);
                    poFieldDefn = new OGRFieldDefn(papszTok[0], OFTInteger);
                }
                else if (numTok >= 2 && EQUAL(papszTok[1], "smallint"))
                {
                    /*-------------------------------------------------
                     * SMALLINT type
                     *------------------------------------------------*/
                    nStatus = m_poDATFile->ValidateFieldInfoFromTAB(iField, 
                                                               papszTok[0],
                                                               TABFSmallInt,
                                                               0,
                                                               0);
                    poFieldDefn = new OGRFieldDefn(papszTok[0], OFTInteger);
                }
                else if (numTok >= 4 && EQUAL(papszTok[1], "decimal"))
                {
                    /*-------------------------------------------------
                     * DECIMAL type
                     *------------------------------------------------*/
                    nStatus = m_poDATFile->ValidateFieldInfoFromTAB(iField, 
                                                               papszTok[0],
                                                               TABFDecimal,
                                                           atoi(papszTok[2]),
                                                           atoi(papszTok[3]));
                    poFieldDefn = new OGRFieldDefn(papszTok[0], OFTReal);
                    poFieldDefn->SetWidth(atoi(papszTok[2]));
                    poFieldDefn->SetPrecision(atoi(papszTok[3]));
                }
                else if (numTok >= 2 && EQUAL(papszTok[1], "float"))
                {
                    /*-------------------------------------------------
                     * FLOAT type
                     *------------------------------------------------*/
                    nStatus = m_poDATFile->ValidateFieldInfoFromTAB(iField, 
                                                               papszTok[0],
                                                               TABFFloat,
                                                               0, 0);
                    poFieldDefn = new OGRFieldDefn(papszTok[0], OFTReal);
                }
                else if (numTok >= 2 && EQUAL(papszTok[1], "date"))
                {
                    /*-------------------------------------------------
                     * DATE type (returned as a string: "DD/MM/YYYY")
                     *------------------------------------------------*/
                    nStatus = m_poDATFile->ValidateFieldInfoFromTAB(iField, 
                                                               papszTok[0],
                                                               TABFDate,
                                                               0,
                                                               0);
                    poFieldDefn = new OGRFieldDefn(papszTok[0], OFTString);
                    poFieldDefn->SetWidth(10);
                }
                else if (numTok >= 2 && EQUAL(papszTok[1], "logical"))
                {
                    /*-------------------------------------------------
                     * LOGICAL type (value "T" or "F")
                     *------------------------------------------------*/
                    nStatus = m_poDATFile->ValidateFieldInfoFromTAB(iField, 
                                                               papszTok[0],
                                                               TABFLogical,
                                                               0,
                                                               0);
                    poFieldDefn = new OGRFieldDefn(papszTok[0], OFTString);
                    poFieldDefn->SetWidth(1);
                }
                else 
                    nStatus = -1; // Unrecognized field type or line corrupt

                if (nStatus != 0)
                {
                    CPLError(CE_Failure, CPLE_FileIO,
                     "Failed to parse field definition at line %d in file %s", 
                             iLine+1, m_pszFname);
                    CSLDestroy(papszTok);
                    return -1;
                }
                /*-----------------------------------------------------
                 * Add the FieldDefn to the FeatureDefn and continue with
                 * the next one.
                 *----------------------------------------------------*/
                m_poDefn->AddFieldDefn(poFieldDefn);
                poFieldDefn = NULL;
            }

            bInsideTableDef = FALSE;
        }/* end of fields section*/
        else
        {
            // Simply Ignore unrecognized lines
        }
    }

    CSLDestroy(papszTok);

    return 0;
}


/**********************************************************************
 *                   TABFile::Close()
 *
 * Close current file, and release all memory used.
 *
 * Returns 0 on success, -1 on error.
 **********************************************************************/
int TABFile::Close()
{
    if (m_poMAPFile == NULL)
        return 0;

    //__TODO__ Commit the latest changes to the file...
    
    if (m_poMAPFile)
    {
        m_poMAPFile->Close();
        delete m_poMAPFile;
        m_poMAPFile = NULL;
    }

    if (m_poDATFile)
    {
        m_poDATFile->Close();
        delete m_poDATFile;
        m_poDATFile = NULL;
    }

    if (m_poCurFeature)
    {
        delete m_poCurFeature;
        m_poCurFeature = NULL;
    }

    if (m_poDefn)
    {
        delete m_poDefn;
        m_poDefn = NULL;
    }

    if (m_poSpatialRef)
    {
        delete m_poSpatialRef;
        m_poSpatialRef = NULL;
    }

    CPLFree(m_papszTABFile);
    m_papszTABFile = NULL;

    CPLFree(m_pszFname);
    m_pszFname = NULL;

    m_nCurFeatureId = -1;
    m_nLastFeatureId = -1;

    return 0;
}


/**********************************************************************
 *                   TABFile::GetNextFeatureId()
 *
 * Returns feature id that follows nPrevId, or -1 if it is the
 * last feature id.  Pass nPrevId=-1 to fetch the first valid feature id.
 **********************************************************************/
int TABFile::GetNextFeatureId(int nPrevId)
{
    if (nPrevId <= 0 && m_nLastFeatureId > 0)
        return 1;       // Feature Ids start at 1
    else if (nPrevId > 0 && nPrevId < m_nLastFeatureId)
        return nPrevId + 1;
    else
        return -1;

    return 0;
}

/**********************************************************************
 *                   TABFile::GetFeatureRef()
 *
 * Fill and return a TABFeature object for the specified feature id.
 *
 * The retruned pointer is a reference to an object owned and maintained
 * by this TABFile object.  It should not be altered or freed by the 
 * caller and its contents is guaranteed to be valid only until the next
 * call to GetFeatureRef() or Close().
 *
 * Returns NULL if the specified feature id does not exist of if an
 * error happened.  In any case, CPLError() will have been called to
 * report the reason of the failure.
 **********************************************************************/
TABFeature *TABFile::GetFeatureRef(int nFeatureId)
{
    /*-----------------------------------------------------------------
     * Make sure file is opened and Validate feature id by positioning
     * the read pointers for the .MAP and .DAT files to this feature id.
     *----------------------------------------------------------------*/
    if (m_poMAPFile == NULL)
    {
        CPLError(CE_Failure, CPLE_IllegalArg,
                 "GetFeatureRef() failed: file is not opened!");
        return NULL;
    }

    if (nFeatureId <= 0 || nFeatureId > m_nLastFeatureId ||
        m_poMAPFile->MoveToObjId(nFeatureId) != 0 ||
        m_poDATFile->GetRecordBlock(nFeatureId) == NULL )
    {
        CPLError(CE_Failure, CPLE_IllegalArg,
                 "GetFeatureRef() failed: invalid feature id %d", 
                 nFeatureId);
        return NULL;
    }

    /*-----------------------------------------------------------------
     * Flush current feature object
     * __TODO__ try to reuse if it is already of the right type
     *----------------------------------------------------------------*/
    if (m_poCurFeature)
    {
        delete m_poCurFeature;
        m_poCurFeature = NULL;
    }

    /*-----------------------------------------------------------------
     * Create new feature object of the right type
     *----------------------------------------------------------------*/
    switch(m_poMAPFile->GetCurObjType())
    {
      case TAB_GEOM_SYMBOL_C:
      case TAB_GEOM_SYMBOL:
        m_poCurFeature = new TABPoint(m_poDefn);
        break;
      case TAB_GEOM_FONTSYMBOL_C:
      case TAB_GEOM_FONTSYMBOL:
        m_poCurFeature = new TABFontPoint(m_poDefn);
        break;
      case TAB_GEOM_CUSTOMSYMBOL_C:
      case TAB_GEOM_CUSTOMSYMBOL:
        m_poCurFeature = new TABCustomPoint(m_poDefn);
        break;
      case TAB_GEOM_LINE_C:
      case TAB_GEOM_LINE:
      case TAB_GEOM_PLINE_C:
      case TAB_GEOM_PLINE:
      case TAB_GEOM_MULTIPLINE_C:
      case TAB_GEOM_MULTIPLINE:
       m_poCurFeature = new TABPolyline(m_poDefn);
        break;
      case TAB_GEOM_ARC_C:
      case TAB_GEOM_ARC:
        m_poCurFeature = new TABArc(m_poDefn);
        break;

      case TAB_GEOM_REGION_C:
      case TAB_GEOM_REGION:
        m_poCurFeature = new TABRegion(m_poDefn);
        break;
      case TAB_GEOM_RECT_C:
      case TAB_GEOM_RECT:
      case TAB_GEOM_ROUNDRECT_C:
      case TAB_GEOM_ROUNDRECT:
        m_poCurFeature = new TABRectangle(m_poDefn);
        break;
      case TAB_GEOM_ELLIPSE_C:
      case TAB_GEOM_ELLIPSE:
        m_poCurFeature = new TABEllipse(m_poDefn);
        break;
      case TAB_GEOM_TEXT_C:
      case TAB_GEOM_TEXT:
        m_poCurFeature = new TABText(m_poDefn);
        break;
      default:
//        m_poCurFeature = new TABDebugFeature(m_poDefn);

        CPLError(CE_Failure, CPLE_NotSupported,
                 "Unsupported object type %d (0x%2.2x)", 
                 m_poMAPFile->GetCurObjType(), m_poMAPFile->GetCurObjType() );
        return NULL;
    }

    /*-----------------------------------------------------------------
     * Read fields from the .DAT file
     * GetRecordBlock() has already been called above...
     *----------------------------------------------------------------*/
    if (m_poCurFeature->ReadRecordFromDATFile(m_poDATFile) != 0)
    {
        delete m_poCurFeature;
        m_poCurFeature = NULL;
        return NULL;
    }

    /*-----------------------------------------------------------------
     * Read geometry from the .MAP file
     * MoveToObjId() has already been called above...
     *----------------------------------------------------------------*/
    if (m_poCurFeature->ReadGeometryFromMAPFile(m_poMAPFile) != 0)
    {
        delete m_poCurFeature;
        m_poCurFeature = NULL;
        return NULL;
    }


    return m_poCurFeature;
}

/**********************************************************************
 *                   TABFile::GetFeatureDefn()
 *
 * Returns a reference to the OGRFeatureDefn that will be used to create
 * features in this dataset.
 *
 * Returns a reference to an object that is maintained by this TABFile
 * object (and thus should not be modified or freed by the caller) or
 * NULL if the OGRFeatureDefn has not been initialized yet (i.e. no file
 * opened yet)
 **********************************************************************/
OGRFeatureDefn *TABFile::GetFeatureDefn()
{
    return m_poDefn;
}

/**********************************************************************
 *                   TABFile::GetNativeFieldType()
 *
 * Returns the native MapInfo field type for the specified field.
 *
 * Returns TABFUnknown if file is not opened, or if specified field index is
 * invalid.
 **********************************************************************/
TABFieldType TABFile::GetNativeFieldType(int nFieldId)
{
    if (m_poDATFile)
    {
        return m_poDATFile->GetFieldType(nFieldId);
    }
    return TABFUnknown;
}

/**********************************************************************
 *                   TABFile::GetSpatialRef()
 *
 * Returns a reference to an OGRSpatialReference for this dataset.
 * If the projection parameters have not been parsed yet, then we will
 * parse them before returning.
 *
 * The returned object is owned and maintained by this TABFile and
 * should not be modified or freed by the caller.
 *
 * Returns NULL if the SpatialRef cannot be accessed.
 **********************************************************************/
OGRSpatialReference *TABFile::GetSpatialRef()
{
    if (m_poMAPFile == NULL )
    {
        CPLError(CE_Failure, CPLE_AssertionFailed,
                 "GetSpatialRef() failed: file has not been opened yet.");
        return NULL;
    }

    /*-----------------------------------------------------------------
     * If projection params have already been processed, just use them.
     *----------------------------------------------------------------*/
    if (m_poSpatialRef != NULL)
        return m_poSpatialRef;
    

    /*-----------------------------------------------------------------
     * Fetch the parameters from the header.
     *----------------------------------------------------------------*/
    TABMAPHeaderBlock *poHeader;
    TABProjInfo     sTABProj;

    if ((poHeader = m_poMAPFile->GetHeaderBlock()) == NULL ||
        poHeader->GetProjInfo( &sTABProj ) != 0)
    {
        CPLError(CE_Failure, CPLE_FileIO,
                 "GetSpatialRef() failed reading projection parameters.");
        return NULL;
    }

    /*-----------------------------------------------------------------
     * Transform them into an OGRSpatialReference.
     *----------------------------------------------------------------*/
    m_poSpatialRef = new OGRSpatialReference;

    /*-----------------------------------------------------------------
     * Handle the PROJCS style projections, but add the datum later.
     *----------------------------------------------------------------*/
    switch( sTABProj.nProjId )
    {
      /*--------------------------------------------------------------
       * lat/long .. just add the GEOGCS later.
       *-------------------------------------------------------------*/
      case 1:
        break;

      /*--------------------------------------------------------------
       * Lambert Conic Conformal
       *-------------------------------------------------------------*/
      case 3:
        m_poSpatialRef->SetLCC( sTABProj.adProjParams[2],
                                sTABProj.adProjParams[3],
                                sTABProj.adProjParams[1],
                                sTABProj.adProjParams[0],
                                sTABProj.adProjParams[4],
                                sTABProj.adProjParams[5] );
        break;

      /*--------------------------------------------------------------
       * Lambert Azimuthal Equal Area
       *-------------------------------------------------------------*/
      case 4:
        m_poSpatialRef->SetLAEA( sTABProj.adProjParams[1],
                                 sTABProj.adProjParams[0],
                                 0.0, 0.0 );
        break;

      /*--------------------------------------------------------------
       * Azimuthal Equidistant (Polar aspect only)
       *-------------------------------------------------------------*/
      case 5:
        m_poSpatialRef->SetAE( sTABProj.adProjParams[1],
                               sTABProj.adProjParams[0],
                               0.0, 0.0 );
        break;

      /*--------------------------------------------------------------
       * Equidistant Conic
       *-------------------------------------------------------------*/
      case 6:
        m_poSpatialRef->SetEC( sTABProj.adProjParams[2],
                               sTABProj.adProjParams[3],
                               sTABProj.adProjParams[1],
                               sTABProj.adProjParams[0],
                               sTABProj.adProjParams[4],
                               sTABProj.adProjParams[5] );
        break;

      /*--------------------------------------------------------------
       * Hotine Oblique Mercator
       *-------------------------------------------------------------*/
      case 7:
        m_poSpatialRef->SetHOM( sTABProj.adProjParams[1],
                                sTABProj.adProjParams[0], 
                                sTABProj.adProjParams[2],
                                0.0, 
                                sTABProj.adProjParams[3],
                                sTABProj.adProjParams[4],
                                sTABProj.adProjParams[5] );
        break;

      /*--------------------------------------------------------------
       * Albers Conic Equal Area
       *-------------------------------------------------------------*/
      case 9:
        m_poSpatialRef->SetACEA( sTABProj.adProjParams[2],
                                 sTABProj.adProjParams[3],
                                 sTABProj.adProjParams[1],
                                 sTABProj.adProjParams[0],
                                 sTABProj.adProjParams[4],
                                 sTABProj.adProjParams[5] );
        break;

      /*--------------------------------------------------------------
       * Mercator
       *-------------------------------------------------------------*/
      case 10:
        m_poSpatialRef->SetMercator( 0.0, sTABProj.adProjParams[0],
                                     1.0, 0.0, 0.0 );
        break;

      /*--------------------------------------------------------------
       * Miller Cylindrical
       *-------------------------------------------------------------*/
      case 11:
        m_poSpatialRef->SetMC( 0.0, sTABProj.adProjParams[0],
                               0.0, 0.0 );
        break;

      /*--------------------------------------------------------------
       * Robinson
       *-------------------------------------------------------------*/
      case 12:
        m_poSpatialRef->SetRobinson( sTABProj.adProjParams[0],
                                     0.0, 0.0 );
        break;

      /*--------------------------------------------------------------
       * Sinusoidal
       *-------------------------------------------------------------*/
      case 16:
        m_poSpatialRef->SetSinusoidal( sTABProj.adProjParams[0],
                                       0.0, 0.0 );
        break;

      /*--------------------------------------------------------------
       * Transverse Mercator
       *-------------------------------------------------------------*/
      case 8:
      case 21:
      case 22:
      case 23:
      case 24:
        m_poSpatialRef->SetTM( sTABProj.adProjParams[1],
                               sTABProj.adProjParams[0],
                               sTABProj.adProjParams[2],
                               sTABProj.adProjParams[3],
                               sTABProj.adProjParams[4] );
        break;

      /*--------------------------------------------------------------
       * New Zealand Map Grid
       *-------------------------------------------------------------*/
      case 18:
        m_poSpatialRef->SetNZMG( sTABProj.adProjParams[1],
                                 sTABProj.adProjParams[0],
                                 sTABProj.adProjParams[2],
                                 sTABProj.adProjParams[3] );
        break;

      /*--------------------------------------------------------------
       * Lambert Conic Conformal (Belgium)
       *-------------------------------------------------------------*/
      case 19:
        m_poSpatialRef->SetLCCB( sTABProj.adProjParams[2],
                                 sTABProj.adProjParams[3],
                                 sTABProj.adProjParams[1],
                                 sTABProj.adProjParams[0],
                                 sTABProj.adProjParams[4],
                                 sTABProj.adProjParams[5] );
        break;

      /*--------------------------------------------------------------
       * Stereographic
       *-------------------------------------------------------------*/
      case 20:
        m_poSpatialRef->SetStereographic( 0.0, sTABProj.adProjParams[0], 
                                          1.0,
                                          sTABProj.adProjParams[1],
                                          sTABProj.adProjParams[2] );
        break;

      /*--------------------------------------------------------------
       * Cylindrical Equal Area
       *-------------------------------------------------------------*/
      case 2:

      /*--------------------------------------------------------------
       * Mollweide
       *-------------------------------------------------------------*/
      case 13:

      /*--------------------------------------------------------------
       * Eckert IV
       *-------------------------------------------------------------*/
      case 14:

      /*--------------------------------------------------------------
       * Eckert VI
       *-------------------------------------------------------------*/
      case 15:

      /*--------------------------------------------------------------
       * Gall
       *-------------------------------------------------------------*/
      case 17:

      default:
        break;
    }

    /*-----------------------------------------------------------------
     * Collect units definition.
     *----------------------------------------------------------------*/
    if( sTABProj.nProjId != 1 && m_poSpatialRef->GetRoot() != NULL )
    {
        OGR_SRSNode	*poUnits = new OGR_SRSNode("UNIT");
        
        m_poSpatialRef->GetRoot()->AddChild(poUnits);

        poUnits->AddChild( new OGR_SRSNode( SRS_UL_METER ) );
        poUnits->AddChild( new OGR_SRSNode( "1.0" ) );
       
        switch( sTABProj.nUnitsId )
        {
          case 1:
            poUnits->GetChild(0)->SetValue("Kilometer");
            poUnits->GetChild(1)->SetValue("1000.0");
            break;
            
          case 2:
            poUnits->GetChild(0)->SetValue("Inch");
            poUnits->GetChild(1)->SetValue("0.0254");
            break;
            
          case 3:
            poUnits->GetChild(0)->SetValue(SRS_UL_FOOT);
            poUnits->GetChild(1)->SetValue(SRS_UL_FOOT_CONV);
            break;
            
          case 4:
            poUnits->GetChild(0)->SetValue("Yard");
            poUnits->GetChild(1)->SetValue("0.9144");
            break;
            
          case 5:
            poUnits->GetChild(0)->SetValue("Millimeter");
            poUnits->GetChild(1)->SetValue("0.001");
            break;
            
          case 6:
            poUnits->GetChild(0)->SetValue("Centimeter");
            poUnits->GetChild(1)->SetValue("0.01");
            break;
            
          case 7:
            poUnits->GetChild(0)->SetValue(SRS_UL_METER);
            poUnits->GetChild(1)->SetValue("1.0");
            break;
            
          case 8:
            poUnits->GetChild(0)->SetValue(SRS_UL_US_FOOT);
            poUnits->GetChild(1)->SetValue(SRS_UL_US_FOOT_CONV);
            break;
            
          case 9:
            poUnits->GetChild(0)->SetValue(SRS_UL_NAUTICAL_MILE);
            poUnits->GetChild(1)->SetValue(SRS_UL_NAUTICAL_MILE_CONV);
            break;
            
          case 30:
            poUnits->GetChild(0)->SetValue(SRS_UL_LINK);
            poUnits->GetChild(1)->SetValue(SRS_UL_LINK_CONV);
            break;
            
          case 31:
            poUnits->GetChild(0)->SetValue(SRS_UL_CHAIN);
            poUnits->GetChild(1)->SetValue(SRS_UL_CHAIN_CONV);
            break;
            
          case 32:
            poUnits->GetChild(0)->SetValue(SRS_UL_ROD);
            poUnits->GetChild(1)->SetValue(SRS_UL_ROD_CONV);
            break;
            
          default:
            break;
        }
    }

    /*-----------------------------------------------------------------
     * Create a GEOGCS definition.
     *----------------------------------------------------------------*/
    OGR_SRSNode	*poGCS, *poDatum, *poSpheroid, *poPM;
    char	szDatumName[128];

    poGCS = new OGR_SRSNode("GEOGCS");

    if( m_poSpatialRef->GetRoot() == NULL )
        m_poSpatialRef->SetRoot( poGCS );
    else
        m_poSpatialRef->GetRoot()->AddChild( poGCS );

    poGCS->AddChild( new OGR_SRSNode("unnamed") );

    /*-----------------------------------------------------------------
     * Set the datum.  We are only given the X, Y and Z shift for
     * the datum, so for now we just synthesize a name from this.
     * It would be better if we could lookup a name based on the shift.
     *----------------------------------------------------------------*/
    poGCS->AddChild( (poDatum = new OGR_SRSNode("DATUM")) );

    sprintf( szDatumName, "MapInfo (%.4f,%.4f,%.4f)",
             sTABProj.dDatumShiftX, 
             sTABProj.dDatumShiftY, 
             sTABProj.dDatumShiftZ );
             
    poDatum->AddChild( new OGR_SRSNode(szDatumName) );

    /*-----------------------------------------------------------------
     * Set the spheroid.
     *----------------------------------------------------------------*/
    poDatum->AddChild( (poSpheroid = new OGR_SRSNode("SPHEROID")) );

    poSpheroid->AddChild( new OGR_SRSNode( "GRS_1980" ) );
    poSpheroid->AddChild( new OGR_SRSNode( "6378137" ) );
    poSpheroid->AddChild( new OGR_SRSNode( "298.257222101" ) );

    /* 
    switch( sTABProj.nEllipsoidId )
    {
    }
    */

    /*-----------------------------------------------------------------
     * It seems that the prime meridian is always Greenwich for Mapinfo
     *----------------------------------------------------------------*/
    
    poDatum->AddChild( (poPM = new OGR_SRSNode("PRIMEM")) );

    poPM->AddChild( new OGR_SRSNode("Greenwich") );
    poPM->AddChild( new OGR_SRSNode("0") );
                    
    /*-----------------------------------------------------------------
     * GeogCS is always in degrees.
     *----------------------------------------------------------------*/
    OGR_SRSNode	*poUnit;

    poDatum->AddChild( (poUnit = new OGR_SRSNode("UNIT")) );

    poUnit->AddChild( new OGR_SRSNode(SRS_UA_DEGREE) );
    poUnit->AddChild( new OGR_SRSNode(SRS_UA_DEGREE_CONV) );

    return m_poSpatialRef;
}



/**********************************************************************
 *                   TABFile::Dump()
 *
 * Dump block contents... available only in DEBUG mode.
 **********************************************************************/
#ifdef DEBUG

void TABFile::Dump(FILE *fpOut /*=NULL*/)
{
    if (fpOut == NULL)
        fpOut = stdout;

    fprintf(fpOut, "----- TABFile::Dump() -----\n");

    if (m_poMAPFile == NULL)
    {
        fprintf(fpOut, "File is not opened.\n");
    }
    else
    {
        fprintf(fpOut, "File is opened: %s\n", m_pszFname);
        fprintf(fpOut, "Associated .DAT file ...\n\n");
        m_poDATFile->Dump(fpOut);
        fprintf(fpOut, "... end of .DAT file dump.\n\n");
        if( GetSpatialRef() != NULL )
        {
            char	*pszWKT;

            GetSpatialRef()->exportToWkt( &pszWKT );
            fprintf( fpOut, "SRS = %s\n", pszWKT );
            OGRFree( pszWKT );						
        }
        fprintf(fpOut, "Associated .MAP file ...\n\n");
        m_poMAPFile->Dump(fpOut);
        fprintf(fpOut, "... end of .MAP file dump.\n\n");

    }

    fflush(fpOut);
}

#endif // DEBUG
