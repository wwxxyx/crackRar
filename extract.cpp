#include "rar.hpp"

CmdExtract::CmdExtract()
{
  TotalFileCount=0;
  *Password=0;
  Unp=new Unpack(&DataIO);
  Unp->Init(NULL);
}


CmdExtract::~CmdExtract()
{
  delete Unp;
  memset(Password,0,sizeof(Password));
}


void CmdExtract::DoExtract(CommandData *Cmd)
{
  PasswordCancelled=false;
  DataIO.SetCurrentCommand(*Cmd->Command);

  struct FindData FD;
  //Cmd->GetArcName()得到参数名字ArcName ere.rar		ArcNameW = NULL
  while (Cmd->GetArcName(ArcName,ArcNameW,sizeof(ArcName)))
  {
    if (FindFile::FastFind(ArcName,ArcNameW,&FD))
    	 //FD.Size=88	DataIO.TotalArcSize=88
      DataIO.TotalArcSize+=FD.Size;
  }
 // printf("*************%d*************\n",DataIO.TotalArcSize);

  Cmd->ArcNames->Rewind();

  /*
   *
   * ExtractArchive(Cmd)
   * do the main work
   *
   */
  //printf("test %s& %s&\n",ArcName,ArcNameW);
  //ArcName ere.rar		ArcNameW = NULL
  //循环作用？
  //FastFind（） &&  ExtractArchive()	？？
  while (Cmd->GetArcName(ArcName,ArcNameW,sizeof(ArcName)))
  {
	  //printf("test %s& %s&\n",ArcName,ArcNameW);	//只有一次进入
    while (true)
    {
      char PrevCmdPassword[MAXPASSWORD];
      strcpy(PrevCmdPassword,Cmd->Password);	//PrevCmdPassword "1234"

      /********do the work*******************/
      EXTRACT_ARC_CODE Code=ExtractArchive(Cmd);

      // restore Cmd->Password which could be changed in IsArchive() call
      // for next header encrypted archive
      strcpy(Cmd->Password,PrevCmdPassword);

      if (Code!=EXTRACT_ARC_REPEAT)
        break;
    }
    if (FindFile::FastFind(ArcName,ArcNameW,&FD))
      DataIO.ProcessedArcSize+=FD.Size;
  }

  	  //根据参数结果做判断输出   Cmd->Command!=I,Cmd->DisableDone=0,TotalFileCount!=0
  	  // display
  if (TotalFileCount==0 && *Cmd->Command!='I')
  {
    if (!PasswordCancelled)
    {
      mprintf(St(MExtrNoFiles));
    }
    ErrHandler.SetErrorCode(WARNING);
  }
#ifndef GUI
  else
    if (!Cmd->DisableDone)
      if (*Cmd->Command=='I')
        mprintf(St(MDone));
      else
        if (ErrHandler.GetErrorCount()==0)
          mprintf(St(MExtrAllOk));		//output	"\nAll OK"
        else
          mprintf(St(MExtrTotalErr),ErrHandler.GetErrorCount());
#endif
}


void CmdExtract::ExtractArchiveInit(CommandData *Cmd,Archive &Arc)
{
  DataIO.UnpArcSize=Arc.FileLength();

  FileCount=0;
  MatchedArgs=0;
#ifndef SFX_MODULE
  FirstFile=true;
#endif

  if (*Cmd->Password!=0)
    strcpy(Password,Cmd->Password);
  PasswordAll=(*Cmd->Password!=0);

  DataIO.UnpVolume=false;

  PrevExtracted=false;
  SignatureFound=false;
  AllMatchesExact=true;
  ReconstructDone=false;

  StartTime.SetCurrentTime();
}


EXTRACT_ARC_CODE CmdExtract::ExtractArchive(CommandData *Cmd)
{
  Archive Arc(Cmd);

  /*
   * Arc.WOpen(ArcName,ArcNameW) get the file handle
   * get it!!!
   *
   * file handle's data
   * class File:Arc.hFile   (FileHandle hFile;)
   * get the hFile, just the description of the file
   *
   * put files to the : static File *CreatedFiles[256];
   */
  if (!Arc.WOpen(ArcName,ArcNameW))
  {
    ErrHandler.SetErrorCode(OPEN_ERROR);
    return(EXTRACT_ARC_NEXT);
  }

  //printf("\ntest6******	%d	%d\n",UINT32(DataIO.UnpFileCRC),UINT32(Arc.NewLhd.FileCRC^0xffffffff));

  //********** Arc.IsArchive(true)给Arc.NewLhd.FileCRC赋值，if不进入,[delete]
  if (!Arc.IsArchive(true))
  {
#ifndef GUI
    mprintf(St(MNotRAR),ArcName);
#endif
    if (CmpExt(ArcName,"rar"))
      ErrHandler.SetErrorCode(WARNING);
    return(EXTRACT_ARC_NEXT);
  }

  // archive with corrupt encrypted header can be closed in IsArchive() call
  if (!Arc.IsOpened())
    return(EXTRACT_ARC_NEXT);

#ifndef SFX_MODULE
  if (Arc.Volume && Arc.NotFirstVolume)		  	  //没执行
  {
    char FirstVolName[NM];
    VolNameToFirstName(ArcName,FirstVolName,(Arc.NewMhd.Flags & MHD_NEWNUMBERING));
    //printf("test6   %s\n",FirstVolName);

    // If several volume names from same volume set are specified
    // and current volume is not first in set and first volume is present
    // and specified too, let's skip the current volume.
    if (stricomp(ArcName,FirstVolName)!=0 && FileExist(FirstVolName) &&
        Cmd->ArcNames->Search(FirstVolName,NULL,false))
      return(EXTRACT_ARC_NEXT);
  }
#endif

  if (Arc.Volume)	//没执行
  {
    // Calculate the total size of all accessible volumes.
    // This size is necessary to display the correct total progress indicator.
	  //printf("test6\n");
    char NextName[NM];
    wchar NextNameW[NM];

    strcpy(NextName,Arc.FileName);
    strcpyw(NextNameW,Arc.FileNameW);

    while (true)
    {
      // First volume is already added to DataIO.TotalArcSize 
      // in initial TotalArcSize calculation in DoExtract.
      // So we skip it and start from second volume.
      NextVolumeName(NextName,NextNameW,ASIZE(NextName),(Arc.NewMhd.Flags & MHD_NEWNUMBERING)==0 || Arc.OldFormat);
      struct FindData FD;
      if (FindFile::FastFind(NextName,NextNameW,&FD))
        DataIO.TotalArcSize+=FD.Size;
      else
        break;
    }
  }
  ExtractArchiveInit(Cmd,Arc);
  if (*Cmd->Command=='T' || *Cmd->Command=='I')
    Cmd->Test=true;
  //printf("test6  %c\n",*Cmd->Command);

#ifndef GUI
  	  //Command='I'代表失败，其余成功
  if (*Cmd->Command=='I')
    Cmd->DisablePercentage=true;
  else
	  //Test=true	为测试打印,false为解压打印
    if (Cmd->Test)
      mprintf(St(MExtrTest),ArcName);
    else
      mprintf(St(MExtracting),ArcName);
#endif

  Arc.ViewComment();

  // RAR can close a corrupt encrypted archive
  if (!Arc.IsOpened())
    return(EXTRACT_ARC_NEXT);

  //printf("\ntest&&&&&&&&&\n");	循环前只执行一次
  while (1)
  {
	//printf("\ntest6******    %d	 %d\n",UINT32(DataIO.UnpFileCRC),UINT32(Arc.NewLhd.FileCRC^0xffffffff));
	  //第一次Size=45    第二次Size=7(代表结束)
	 /*
	  * Arc.ReadHeader() 计算出DataIO.UnpFileCRC值，解压后到CRC校验码
	  * 计算 HeaderCRC 值,the original CRC
	  *
	  *ExtractCurrentFile()测试能不能提取解压文件，从压缩里面到第一个文件测试到最后一个文件
	  *进入if中表示测试所有文件结束，Size=7  Repeat=false
	  *ExtractCurrentFile() do the main work
	  *
	  */
    int Size=Arc.ReadHeader();
    bool Repeat=false;
    if (!ExtractCurrentFile(Cmd,Arc,Size,Repeat))
    {
    	if (Repeat)
      {
    		return(EXTRACT_ARC_REPEAT);
      }
      else
        break;
    }
  }
  return(EXTRACT_ARC_NEXT);
}


bool CmdExtract::ExtractCurrentFile(CommandData *Cmd,Archive &Arc,int HeaderSize,bool &Repeat)
{
  char Command=*Cmd->Command;
  if (HeaderSize<=0)
    if (DataIO.UnpVolume)
    {
#ifdef NOVOLUME
      return(false);
#else
      if (!MergeArchive(Arc,&DataIO,false,Command))
      {
        ErrHandler.SetErrorCode(WARNING);
        return(false);
      }
      SignatureFound=false;
#endif
    }
    else
      return(false);
  int HeadType=Arc.GetHeaderType();
  //printf("test6  %d\n",HeadType);		//116(0x74)	第二次：123(0x7B)
  //FILE_HEAD=0x74=116
  //??HeadType为什么要116？
  if (HeadType!=FILE_HEAD)
  {
    if (HeadType==AV_HEAD || HeadType==SIGN_HEAD)
      SignatureFound=true;
#if !defined(SFX_MODULE) && !defined(_WIN_CE)
    if (HeadType==SUB_HEAD && PrevExtracted)
      SetExtraInfo(Cmd,Arc,DestFileName,*DestFileNameW ? DestFileNameW:NULL);
#endif
    if (HeadType==NEWSUB_HEAD)
    {
      if (Arc.SubHead.CmpName(SUBHEAD_TYPE_AV))
        SignatureFound=true;
#if !defined(NOSUBBLOCKS) && !defined(_WIN_CE)
      if (PrevExtracted)
        SetExtraInfoNew(Cmd,Arc,DestFileName,*DestFileNameW ? DestFileNameW:NULL);
#endif
    }
    //ENDARC_HEAD=0x7b
    //printf("test7 %d  %d\n",HeadType,ENDARC_HEAD);   //return false
    if (HeadType==ENDARC_HEAD)
    {

      if (Arc.EndArcHead.Flags & EARC_NEXT_VOLUME)
      {
#ifndef NOVOLUME
        if (!MergeArchive(Arc,&DataIO,false,Command))
        {
          ErrHandler.SetErrorCode(WARNING);
          return(false);
        }
        SignatureFound=false;
#endif
        Arc.Seek(Arc.CurBlockPos,SEEK_SET);
        return(true);
      }
      else
      {
        return(false);
      }
    }
    Arc.SeekToNext();
    return(true);
  }
  PrevExtracted=false;

  if (SignatureFound ||
      !Cmd->Recurse && MatchedArgs>=Cmd->FileArgs->ItemsCount() &&
      AllMatchesExact)
    return(false);

  char ArcFileName[NM];
  IntToExt(Arc.NewLhd.FileName,Arc.NewLhd.FileName);
  strcpy(ArcFileName,Arc.NewLhd.FileName);

  wchar ArcFileNameW[NM];
  *ArcFileNameW=0;

  int MatchType=MATCH_WILDSUBPATH;

  bool EqualNames=false;
  int MatchNumber=Cmd->IsProcessFile(Arc.NewLhd,&EqualNames,MatchType);
  bool ExactMatch=MatchNumber!=0;
#if !defined(SFX_MODULE) && !defined(_WIN_CE)
  if (Cmd->ExclPath==EXCL_BASEPATH)
  {
    *Cmd->ArcPath=0;
    if (ExactMatch)
    {
      Cmd->FileArgs->Rewind();
      if (Cmd->FileArgs->GetString(Cmd->ArcPath,NULL,sizeof(Cmd->ArcPath),MatchNumber-1))
        *PointToName(Cmd->ArcPath)=0;
    }
  }
#endif
  if (ExactMatch && !EqualNames)
    AllMatchesExact=false;

#ifdef UNICODE_SUPPORTED
  //printf("test7\n");
  bool WideName=(Arc.NewLhd.Flags & LHD_UNICODE) && UnicodeEnabled();
#else
  bool WideName=false;
#endif


  wchar *DestNameW=WideName ? DestFileNameW:NULL;

#ifdef UNICODE_SUPPORTED
  if (WideName)
  {
    ConvertPath(Arc.NewLhd.FileNameW,ArcFileNameW);
    char Name[NM];
    if (WideToChar(ArcFileNameW,Name) && IsNameUsable(Name))
      strcpy(ArcFileName,Name);
  }
#endif

  ConvertPath(ArcFileName,ArcFileName);

  if (Arc.IsArcLabel())
    return(true);

  if (Arc.NewLhd.Flags & LHD_VERSION)
  {
    if (Cmd->VersionControl!=1 && !EqualNames)
    {
      if (Cmd->VersionControl==0)
        ExactMatch=false;
      int Version=ParseVersionFileName(ArcFileName,ArcFileNameW,false);
      if (Cmd->VersionControl-1==Version)
        ParseVersionFileName(ArcFileName,ArcFileNameW,true);
      else
        ExactMatch=false;
    }
  }
  else
    if (!Arc.IsArcDir() && Cmd->VersionControl>1)
      ExactMatch=false;

  Arc.ConvertAttributes();

#ifndef SFX_MODULE
  if ((Arc.NewLhd.Flags & (LHD_SPLIT_BEFORE/*|LHD_SOLID*/)) && FirstFile)
  {
    char CurVolName[NM];
    strcpy(CurVolName,ArcName);

    VolNameToFirstName(ArcName,ArcName,(Arc.NewMhd.Flags & MHD_NEWNUMBERING));
    if (stricomp(ArcName,CurVolName)!=0 && FileExist(ArcName))
    {
      *ArcNameW=0;
      Repeat=true;
      return(false);
    }
#if !defined(RARDLL) && !defined(_WIN_CE)
    if (!ReconstructDone)
    {
      ReconstructDone=true;

      RecVolumes RecVol;
      if (RecVol.Restore(Cmd,Arc.FileName,Arc.FileNameW,true))
      {
        Repeat=true;
        return(false);
      }
    }
#endif
    strcpy(ArcName,CurVolName);
  }
#endif
  DataIO.UnpVolume=(Arc.NewLhd.Flags & LHD_SPLIT_AFTER);
  DataIO.NextVolumeMissing=false;

  Arc.Seek(Arc.NextBlockPos-Arc.NewLhd.FullPackSize,SEEK_SET);

  bool TestMode=false;
  bool ExtrFile=false;
  bool SkipSolid=false;

#ifndef SFX_MODULE
  if (FirstFile && (ExactMatch || Arc.Solid) && (Arc.NewLhd.Flags & (LHD_SPLIT_BEFORE/*|LHD_SOLID*/))!=0)
  {
    if (ExactMatch)
    {
      Log(Arc.FileName,St(MUnpCannotMerge),ArcFileName);
#ifdef RARDLL
      Cmd->DllError=ERAR_BAD_DATA;
#endif
      ErrHandler.SetErrorCode(OPEN_ERROR);
    }
    ExactMatch=false;
  }

  FirstFile=false;
#endif

  if (ExactMatch || (SkipSolid=Arc.Solid)!=0)
  {
    if ((Arc.NewLhd.Flags & LHD_PASSWORD)!=0)
#ifndef RARDLL
      if (*Password==0)
#endif
      {
#ifdef RARDLL
        if (*Cmd->Password==0)
          if (Cmd->Callback==NULL ||
              Cmd->Callback(UCM_NEEDPASSWORD,Cmd->UserData,(LPARAM)Cmd->Password,sizeof(Cmd->Password))==-1)
            return(false);
        strcpy(Password,Cmd->Password);

#else
        if (!GetPassword(PASSWORD_FILE,ArcFileName,Password,sizeof(Password)))
        {
          PasswordCancelled=true;
          return(false);
        }
#endif
      }
#if !defined(GUI) && !defined(SILENT)
      else
        if (!PasswordAll && (!Arc.Solid || Arc.NewLhd.UnpVer>=20 && (Arc.NewLhd.Flags & LHD_SOLID)==0))
        {
          eprintf(St(MUseCurPsw),ArcFileName);
          switch(Cmd->AllYes ? 1:Ask(St(MYesNoAll)))
          {
            case -1:
              ErrHandler.Exit(USER_BREAK);
            case 2:
              if (!GetPassword(PASSWORD_FILE,ArcFileName,Password,sizeof(Password)))
              {
                return(false);
              }
              break;
            case 3:
              PasswordAll=true;
              break;
          }
        }
#endif

#ifndef SFX_MODULE
    if (*Cmd->ExtrPath==0 && *Cmd->ExtrPathW!=0)
      WideToChar(Cmd->ExtrPathW,DestFileName);
    else
#endif
      strcpy(DestFileName,Cmd->ExtrPath);

#ifndef SFX_MODULE
    if (Cmd->AppendArcNameToPath)
    {
      strcat(DestFileName,PointToName(Arc.FirstVolumeName));
      SetExt(DestFileName,NULL);
      AddEndSlash(DestFileName);
    }
#endif

    char *ExtrName=ArcFileName;

    bool EmptyName=false;
#ifndef SFX_MODULE
    int Length=strlen(Cmd->ArcPath);
    if (Length>1 && IsPathDiv(Cmd->ArcPath[Length-1]) &&
        strlen(ArcFileName)==Length-1)
      Length--;
    if (Length>0 && strnicomp(Cmd->ArcPath,ArcFileName,Length)==0)
    {
      ExtrName+=Length;
      while (*ExtrName==CPATHDIVIDER)
        ExtrName++;
      if (*ExtrName==0)
        EmptyName=true;
    }
#endif

    bool AbsPaths=Cmd->ExclPath==EXCL_ABSPATH && Command=='X' && IsDriveDiv(':');
    if (AbsPaths)
      *DestFileName=0;

    //printf("test7  %c\n",Command);
    if (Command=='E' || Cmd->ExclPath==EXCL_SKIPWHOLEPATH)
      strcat(DestFileName,PointToName(ExtrName));
    else
      strcat(DestFileName,ExtrName);


    char DiskLetter=etoupper(DestFileName[0]);

    if (AbsPaths && DestFileName[1]=='_' && IsPathDiv(DestFileName[2]) &&
        DiskLetter>='A' && DiskLetter<='Z')
      DestFileName[1]=':';

#ifndef SFX_MODULE
    if (!WideName && *Cmd->ExtrPathW!=0)
    {
      DestNameW=DestFileNameW;
      WideName=true;
      CharToWide(ArcFileName,ArcFileNameW);
    }
#endif

    if (WideName)
    {
      if (*Cmd->ExtrPathW!=0)
        strcpyw(DestFileNameW,Cmd->ExtrPathW);
      else
        CharToWide(Cmd->ExtrPath,DestFileNameW);

#ifndef SFX_MODULE
      if (Cmd->AppendArcNameToPath)
      {
        wchar FileNameW[NM];
        if (*Arc.FirstVolumeNameW!=0)
          strcpyw(FileNameW,Arc.FirstVolumeNameW);
        else
          CharToWide(Arc.FirstVolumeName,FileNameW);
        strcatw(DestFileNameW,PointToName(FileNameW));
        SetExt(DestFileNameW,NULL);
        AddEndSlash(DestFileNameW);
      }
#endif
      wchar *ExtrNameW=ArcFileNameW;
#ifndef SFX_MODULE
      if (Length>0)
      {
        wchar ArcPathW[NM];
        GetWideName(Cmd->ArcPath,Cmd->ArcPathW,ArcPathW);
        Length=strlenw(ArcPathW);
      }
      ExtrNameW+=Length;
      while (*ExtrNameW==CPATHDIVIDER)
        ExtrNameW++;
#endif

      if (AbsPaths)
        *DestFileNameW=0;

      if (Command=='E' || Cmd->ExclPath==EXCL_SKIPWHOLEPATH)
        strcatw(DestFileNameW,PointToName(ExtrNameW));
      else
        strcatw(DestFileNameW,ExtrNameW);

      if (AbsPaths && DestFileNameW[1]=='_' && IsPathDiv(DestFileNameW[2]))
        DestFileNameW[1]=':';
    }
    else
      *DestFileNameW=0;

    ExtrFile=!SkipSolid && !EmptyName && (Arc.NewLhd.Flags & LHD_SPLIT_BEFORE)==0;

    	//判断要不要解压缩ExtrFile=false;
    if ((Cmd->FreshFiles || Cmd->UpdateFiles) && (Command=='E' || Command=='X'))
    {
      struct FindData FD;
      if (FindFile::FastFind(DestFileName,DestNameW,&FD))
      {
        if (FD.mtime >= Arc.NewLhd.mtime)
        {
          // If directory already exists and its modification time is newer 
          // than start of extraction, it is likely it was created 
          // when creating a path to one of already extracted items. 
          // In such case we'll better update its time even if archived 
          // directory is older.

          if (!FD.IsDir || FD.mtime<StartTime)
            ExtrFile=false;
        }
      }
      else
        if (Cmd->FreshFiles)
          ExtrFile=false;
    }

    // skip encrypted file if no password is specified
    if ((Arc.NewLhd.Flags & LHD_PASSWORD)!=0 && *Password==0)
    {
      ErrHandler.SetErrorCode(WARNING);
#ifdef RARDLL
      Cmd->DllError=ERAR_MISSING_PASSWORD;
#endif
      ExtrFile=false;
    }


    if (Arc.NewLhd.UnpVer<13 || Arc.NewLhd.UnpVer>UNP_VER)
    {
#ifndef SILENT
      Log(Arc.FileName,St(MUnknownMeth),ArcFileName);
#ifndef SFX_MODULE
      Log(Arc.FileName,St(MVerRequired),Arc.NewLhd.UnpVer/10,Arc.NewLhd.UnpVer%10);
#endif
#endif
      ExtrFile=false;
      ErrHandler.SetErrorCode(WARNING);
    }

    File CurFile;

    //printf("\n*******************^^^^^^^^^^&&&&&&&&&&&&&&&&&&&&\n%c\n",Command); command='T'
    if (!IsLink(Arc.NewLhd.FileAttr))
      if (Arc.IsArcDir())	/*******into else {}************/
      {
        if (!ExtrFile || Command=='P' || Command=='E' || Cmd->ExclPath==EXCL_SKIPWHOLEPATH)
          return(true);
        if (SkipSolid)
        {
#ifndef GUI
          mprintf(St(MExtrSkipFile),ArcFileName);
#endif
          return(true);
        }
        TotalFileCount++;

        //Test=1,打印test  ok    none???
        if (Cmd->Test)
        {
#ifndef GUI
          mprintf(St(MExtrTestFile),ArcFileName);
          mprintf(" %s",St(MOk));
#endif
          return(true);
        }
        MKDIR_CODE MDCode=MakeDir(DestFileName,DestNameW,Arc.NewLhd.FileAttr);
        bool DirExist=false;
        if (MDCode!=MKDIR_SUCCESS)
        {
          DirExist=FileExist(DestFileName,DestNameW);
          if (DirExist && !IsDir(GetFileAttr(DestFileName,DestNameW)))
          {
            bool UserReject;
            FileCreate(Cmd,NULL,DestFileName,DestNameW,Cmd->Overwrite,&UserReject,Arc.NewLhd.FullUnpSize,Arc.NewLhd.FileTime);
            DirExist=false;
          }
          CreatePath(DestFileName,DestNameW,true);
          MDCode=MakeDir(DestFileName,DestNameW,Arc.NewLhd.FileAttr);
        }
        if (MDCode==MKDIR_SUCCESS)
        {
#ifndef GUI
          mprintf(St(MCreatDir),DestFileName);
          mprintf(" %s",St(MOk));
#endif
          PrevExtracted=true;
        }
        else
          if (DirExist)
          {
            SetFileAttr(DestFileName,DestNameW,Arc.NewLhd.FileAttr);
            PrevExtracted=true;
          }
          else
          {
            Log(Arc.FileName,St(MExtrErrMkDir),DestFileName);
            ErrHandler.SysErrMsg();
#ifdef RARDLL
            Cmd->DllError=ERAR_ECREATE;
#endif
            ErrHandler.SetErrorCode(CREATE_ERROR);
          }
        if (PrevExtracted)
        {
#if defined(_WIN_32) && !defined(_WIN_CE) && !defined(SFX_MODULE)
          if (Cmd->SetCompressedAttr &&
              (Arc.NewLhd.FileAttr & FILE_ATTRIBUTE_COMPRESSED)!=0 && WinNT())
            SetFileCompression(DestFileName,DestNameW,true);
#endif
          SetDirTime(DestFileName,DestNameW,
            Cmd->xmtime==EXTTIME_NONE ? NULL:&Arc.NewLhd.mtime,
            Cmd->xctime==EXTTIME_NONE ? NULL:&Arc.NewLhd.ctime,
            Cmd->xatime==EXTTIME_NONE ? NULL:&Arc.NewLhd.atime);
        }
        return(true);
      }
      else
      {
    	  //printf("\n*******************^^^^^^^^^^&&&&&&&&&&&&&&&&&&&&\n%d %d %c\n",Cmd->Test,ExtrFile,Command);
    	  //Cmd->Test=1 && ExtrFile =1  command="T"
    	  /************set the flag of the command 'T' 'X' 'P'**************/
        if (Cmd->Test && ExtrFile)
          TestMode=true;
#if !defined(GUI) && !defined(SFX_MODULE)
        if (Command=='P' && ExtrFile)
          CurFile.SetHandleType(FILE_HANDLESTD);
#endif
        if ((Command=='E' || Command=='X') && ExtrFile && !Cmd->Test)
        {
          bool UserReject;
          if (!FileCreate(Cmd,&CurFile,DestFileName,DestNameW,Cmd->Overwrite,&UserReject,Arc.NewLhd.FullUnpSize,Arc.NewLhd.FileTime))
          {
            ExtrFile=false;
            if (!UserReject)
            {
              ErrHandler.CreateErrorMsg(Arc.FileName,DestFileName);
              ErrHandler.SetErrorCode(CREATE_ERROR);
#ifdef RARDLL
              Cmd->DllError=ERAR_ECREATE;
#endif
              if (!IsNameUsable(DestFileName))
              {
                Log(Arc.FileName,St(MCorrectingName));
                char OrigName[sizeof(DestFileName)];
                strncpyz(OrigName,DestFileName,ASIZE(OrigName));

                MakeNameUsable(DestFileName,true);
                CreatePath(DestFileName,NULL,true);
                if (FileCreate(Cmd,&CurFile,DestFileName,NULL,Cmd->Overwrite,&UserReject,Arc.NewLhd.FullUnpSize,Arc.NewLhd.FileTime))
                {
#ifndef SFX_MODULE
                  Log(Arc.FileName,St(MRenaming),OrigName,DestFileName);
#endif
                  ExtrFile=true;
                }
                else
                  ErrHandler.CreateErrorMsg(Arc.FileName,DestFileName);
              }
            }
          }
        }
      }

    if (!ExtrFile && Arc.Solid)
    {
      SkipSolid=true;
      TestMode=true;
      ExtrFile=true;
    }
    if (ExtrFile)
    {
      if (!SkipSolid)
      {
        if (!TestMode && Command!='P' && CurFile.IsDevice())
        {
          Log(Arc.FileName,St(MInvalidName),DestFileName);
          ErrHandler.WriteError(Arc.FileName,DestFileName);
        }
        TotalFileCount++;
      }
      FileCount++;
      	  	  //选择测试命令 E T
#ifndef GUI
      if (Command!='I')
        if (SkipSolid)
          mprintf(St(MExtrSkipFile),ArcFileName);
        else
          switch(Cmd->Test ? 'T':Command)
          {
            case 'T':
              mprintf(St(MExtrTestFile),ArcFileName);
              break;
#ifndef SFX_MODULE
            case 'P':
              mprintf(St(MExtrPrinting),ArcFileName);
              break;
#endif
            case 'X':
            case 'E':
              mprintf(St(MExtrFile),DestFileName);
              break;
          }
      if (!Cmd->DisablePercentage)
        mprintf("     ");
#endif
      DataIO.CurUnpRead=0;
      DataIO.CurUnpWrite=0;
      DataIO.UnpFileCRC=Arc.OldFormat ? 0 : 0xffffffff;
      DataIO.PackedCRC=0xffffffff;
      DataIO.SetEncryption(
        (Arc.NewLhd.Flags & LHD_PASSWORD) ? Arc.NewLhd.UnpVer:0,Password,
        (Arc.NewLhd.Flags & LHD_SALT) ? Arc.NewLhd.Salt:NULL,false,
        Arc.NewLhd.UnpVer>=36);
      DataIO.SetPackedSizeToRead(Arc.NewLhd.FullPackSize);
      DataIO.SetFiles(&Arc,&CurFile);
      DataIO.SetTestMode(TestMode);
      DataIO.SetSkipUnpCRC(SkipSolid);
#ifndef _WIN_CE
      if (!TestMode && !Arc.BrokenFileHeader &&
          (Arc.NewLhd.FullPackSize<<11)>Arc.NewLhd.FullUnpSize &&
          (Arc.NewLhd.FullUnpSize<100000000 || Arc.FileLength()>Arc.NewLhd.FullPackSize))
        CurFile.Prealloc(Arc.NewLhd.FullUnpSize);
#endif

      CurFile.SetAllowDelete(!Cmd->KeepBroken);
      bool LinkCreateMode=!Cmd->Test && !SkipSolid;
      if (ExtractLink(DataIO,Arc,DestFileName,DataIO.UnpFileCRC,LinkCreateMode))
      {
        PrevExtracted=LinkCreateMode;
      }
      else
      {
    	/******get in the else************/
        if ((Arc.NewLhd.Flags & LHD_SPLIT_BEFORE)==0)
        {
          if (Arc.NewLhd.Method==0x30)
            UnstoreFile(DataIO,Arc.NewLhd.FullUnpSize);
          else
          {
            Unp->SetDestSize(Arc.NewLhd.FullUnpSize);
            //Arc.NewLhd.UnpVer=29
            /***********************************************
             *
             * Unp->DoUnpack(); do the unpack work
             * call function : Unpack::Unpack29()
             * extract the contests of the whole file
             * and count the CRC( DataIO.UnpFileCRC ) of the file
             *
             * ************************************************/
#ifndef SFX_MODULE
            if (Arc.NewLhd.UnpVer<=15)
              Unp->DoUnpack(15,FileCount>1 && Arc.Solid);
            else
#endif
              Unp->DoUnpack(Arc.NewLhd.UnpVer,Arc.NewLhd.Flags & LHD_SOLID);
          }
        }
      }

      if (Arc.IsOpened())
        Arc.SeekToNext();

      bool BrokenFile=false;
      if (!SkipSolid)
      {
    	/***************************************************************
    	 *
    	 * UINT32(Arc.NewLhd.FileCRC^0xffffffff) is the original CRC
    	 * UINT32(DataIO.UnpFileCRC) is the counted CRC
    	 * == judge if it is the right password of the encrypt file
    	 *
    	 ****************************************************************/
    	//printf("\n^^^^^^^^^^^^^^^ %d	%x	%x %x\n",Arc.OldFormat,UINT32(DataIO.UnpFileCRC),UINT32(Arc.NewLhd.FileCRC),UINT32(Arc.NewLhd.FileCRC^0xffffffff));
        if (Arc.OldFormat && UINT32(DataIO.UnpFileCRC)==UINT32(Arc.NewLhd.FileCRC) ||
            !Arc.OldFormat && UINT32(DataIO.UnpFileCRC)==UINT32(Arc.NewLhd.FileCRC^0xffffffff))
        {
#ifndef GUI
          if (Command!='P' && Command!='I')
            mprintf("%s%s ",Cmd->DisablePercentage ? " ":"\b\b\b\b\b ",St(MOk));
#endif
        }
        else
        {
          char *BadArcName=/*(Arc.NewLhd.Flags & LHD_SPLIT_BEFORE) ? NULL:*/Arc.FileName;
          if (Arc.NewLhd.Flags & LHD_PASSWORD)
          {
            Log(BadArcName,St(MEncrBadCRC),ArcFileName);
          }
          else
          {
            Log(BadArcName,St(MCRCFailed),ArcFileName);
          }
          BrokenFile=true;
          ErrHandler.SetErrorCode(CRC_ERROR);
#ifdef RARDLL
          Cmd->DllError=ERAR_BAD_DATA;
#endif
          Alarm();
        }
      }
#ifndef GUI
      else
        mprintf("\b\b\b\b\b     ");
#endif


      /*
       *
       * extract the file, don't need
       *
       */
      if (!TestMode && (Command=='X' || Command=='E') &&
          !IsLink(Arc.NewLhd.FileAttr))
      {
#if defined(_WIN_32) || defined(_EMX)
        if (Cmd->ClearArc)
          Arc.NewLhd.FileAttr&=~FA_ARCH;
/*
        else
          Arc.NewLhd.FileAttr|=FA_ARCH; //set archive bit for unpacked files (file is not backed up)
*/
#endif
        if (!BrokenFile || Cmd->KeepBroken)
        {
          if (BrokenFile)
            CurFile.Truncate();
          CurFile.SetOpenFileStat(
            Cmd->xmtime==EXTTIME_NONE ? NULL:&Arc.NewLhd.mtime,
            Cmd->xctime==EXTTIME_NONE ? NULL:&Arc.NewLhd.ctime,
            Cmd->xatime==EXTTIME_NONE ? NULL:&Arc.NewLhd.atime);
          CurFile.Close();
#if defined(_WIN_32) && !defined(_WIN_CE) && !defined(SFX_MODULE)
          if (Cmd->SetCompressedAttr &&
              (Arc.NewLhd.FileAttr & FILE_ATTRIBUTE_COMPRESSED)!=0 && WinNT())
            SetFileCompression(CurFile.FileName,CurFile.FileNameW,true);
#endif
          CurFile.SetCloseFileStat(
            Cmd->xmtime==EXTTIME_NONE ? NULL:&Arc.NewLhd.mtime,
            Cmd->xatime==EXTTIME_NONE ? NULL:&Arc.NewLhd.atime,
            Arc.NewLhd.FileAttr);
          PrevExtracted=true;
        }
      }
    }
  }

  if (ExactMatch)
    MatchedArgs++;
  if (DataIO.NextVolumeMissing || !Arc.IsOpened())
    return(false);
  if (!ExtrFile)
    if (!Arc.Solid)
      Arc.SeekToNext();
    else
      if (!SkipSolid)
        return(false);
  return(true);
}


void CmdExtract::UnstoreFile(ComprDataIO &DataIO,Int64 DestUnpSize)
{
  Array<byte> Buffer(0x10000);
  while (1)
  {
    unsigned int Code=DataIO.UnpRead(&Buffer[0],Buffer.Size());
    if (Code==0 || (int)Code==-1)
      break;
    Code=Code<DestUnpSize ? Code:int64to32(DestUnpSize);
    DataIO.UnpWrite(&Buffer[0],Code);
    if (DestUnpSize>=0)
      DestUnpSize-=Code;
  }
}

