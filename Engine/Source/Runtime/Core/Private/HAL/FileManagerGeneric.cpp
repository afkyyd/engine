// Copyright 1998-2016 Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	FileManagerGeneric.cpp: Unreal generic file manager support code.

	This base class simplifies IFileManager implementations by providing
	simple, unoptimized implementations of functions whose implementations
	can be derived from other functions.

=============================================================================*/

// for compression
#include "CorePrivatePCH.h"
#include "FileManagerGeneric.h"
#include "SecureHash.h"
#include "UniquePtr.h"
#include <time.h>

DEFINE_LOG_CATEGORY_STATIC( LogFileManager, Log, All );

#define COPYBLOCKSIZE	32768

/*-----------------------------------------------------------------------------
	File Manager.
-----------------------------------------------------------------------------*/

void FFileManagerGeneric::ProcessCommandLineOptions() 
{
#if !UE_BUILD_SHIPPING
	if( FParse::Param( FCommandLine::Get(),TEXT( "CLEANSCREENSHOTS" ) ) )
	{
		DeleteDirectory( *FPaths::ScreenShotDir(), false, true );
	}

	if( FParse::Param( FCommandLine::Get(),TEXT( "CLEANLOGS" ) ) )
	{
		DeleteDirectory( *FPaths::GameLogDir(), false, true );
	}
#endif
}

FArchive* FFileManagerGeneric::CreateFileReader( const TCHAR* InFilename, uint32 Flags )
{
	IFileHandle* Handle = GetLowLevel().OpenRead( InFilename, !!(Flags & FILEREAD_AllowWrite) );
	if( !Handle )
	{
		if( Flags & FILEREAD_NoFail )
		{
			UE_LOG( LogFileManager, Fatal, TEXT( "Failed to read file: %s" ), InFilename );
		}
		return NULL;
	}
	return new FArchiveFileReaderGeneric( Handle, InFilename, Handle->Size() );
}

/**
* Dummy archive that doesn't actually write anything
* it just updates the file pos when seeking
*/
class FArchiveFileWriterDummy : public FArchive
{
public:
	FArchiveFileWriterDummy()
		:	Pos( 0 )
	{
		ArIsSaving = ArIsPersistent = true;
	}
	virtual ~FArchiveFileWriterDummy()
	{
		Close();
	}
	virtual void Seek( int64 InPos ) override
	{
		Pos = InPos;
	}
	virtual int64 Tell() override
	{
		return Pos;
	}
	virtual void Serialize( void* V, int64 Length ) override
	{
		Pos += Length;
	}
	virtual FString GetArchiveName() const override { return TEXT( "FArchiveFileWriterDummy" ); }
protected:
	int64             Pos;
};

FArchive* FFileManagerGeneric::CreateFileWriter( const TCHAR* Filename, uint32 Flags )
{
	// Only allow writes to files that are not signed 
	// Except if the file is missing( that way corrupt ini files can be autogenerated by deleting them )
	if( FSHA1::GetFileSHAHash( Filename, NULL ) && FileSize( Filename ) != -1 )
	{
		UE_LOG( LogFileManager, Log, TEXT( "Can't write to signed game file: %s" ),Filename );
		return new FArchiveFileWriterDummy();
	}
	MakeDirectory( *FPaths::GetPath(Filename), true );

	if( Flags & FILEWRITE_EvenIfReadOnly )
	{
		GetLowLevel().SetReadOnly( Filename, false );
	}

	IFileHandle* Handle = GetLowLevel().OpenWrite( Filename, !!( Flags & FILEWRITE_Append ), !!( Flags & FILEWRITE_AllowRead ) );
	if( !Handle )
	{
		if( Flags & FILEWRITE_NoFail )
		{
			UE_LOG( LogFileManager, Fatal, TEXT( "Failed to create file: %s" ), Filename );
		}
		return NULL;
	}
	return new FArchiveFileWriterGeneric( Handle, Filename, Handle->Tell() );
}


int64 FFileManagerGeneric::FileSize( const TCHAR* Filename )
{
	return GetLowLevel().FileSize( Filename );
}

uint32 FFileManagerGeneric::Copy( const TCHAR* Dest, const TCHAR* Src, bool Replace, bool EvenIfReadOnly, bool Attributes, FCopyProgress* Progress, EFileRead ReadFlags, EFileWrite WriteFlags)
{
	uint32	Result = COPY_OK;
	if( FPaths::ConvertRelativePathToFull(Dest) == FPaths::ConvertRelativePathToFull(Src) )
	{
		Result = COPY_Fail;
	}
	else if( Progress )
	{
		Result = CopyWithProgress(Dest, Src, Replace, EvenIfReadOnly, Attributes, Progress, ReadFlags, WriteFlags);
	}
	else if( !Replace && GetLowLevel().FileExists(Dest) )
	{
		Result = COPY_Fail;
	}
	else
	{
		if( EvenIfReadOnly )
		{
			GetLowLevel().SetReadOnly(Dest, false );
		}
		MakeDirectory( *FPaths::GetPath(Dest), true );
		EPlatformFileRead PlatformFileRead = (ReadFlags & FILEREAD_AllowWrite) ? EPlatformFileRead::AllowWrite : EPlatformFileRead::None;
		EPlatformFileWrite PlatformFileWrite = (WriteFlags & FILEWRITE_AllowRead) ? EPlatformFileWrite::AllowRead : EPlatformFileWrite::None;
		if( !GetLowLevel().CopyFile(Dest, Src, PlatformFileRead, PlatformFileWrite) )
		{
			Result = COPY_Fail;
		}
	}

	// Restore read-only attribute if required
	if( Result == COPY_OK && Attributes )
	{
		GetLowLevel().SetReadOnly(Dest, GetLowLevel().IsReadOnly(Src) );
	}

	return Result;
}

uint32 FFileManagerGeneric::CopyWithProgress(const TCHAR* InDestFile, const TCHAR* InSrcFile, bool ReplaceExisting, bool EvenIfReadOnly, bool Attributes, FCopyProgress* Progress, EFileRead ReadFlags, EFileWrite WriteFlags)
{
	uint32	Result = COPY_OK;

	// Direct file copier.
	if( Progress->Poll( 0.0 ) )
	{
		FString SrcFile		= InSrcFile;
		FString DestFile	= InDestFile;
	
		FArchive* Src = CreateFileReader( *SrcFile, ReadFlags );
		if( !Src )
		{
			Result = COPY_Fail;
		}
		else
		{
			FArchive* Dest = CreateFileWriter( *DestFile, ( ReplaceExisting ? 0 : FILEWRITE_NoReplaceExisting ) | ( EvenIfReadOnly ? FILEWRITE_EvenIfReadOnly : 0 ) | WriteFlags );
			if( !Dest )
			{
				Result = COPY_Fail;
			}
			else
			{
				int64 Size = Src->TotalSize();
				int64 Percent = 0, NewPercent = 0;
				uint8* Buffer = new uint8[COPYBLOCKSIZE];
				for( int64 Total = 0; Total < Size; Total += sizeof(Buffer) )
				{
					int64 Count = FMath::Min( Size - Total, (int64)sizeof(Buffer) );
					Src->Serialize( Buffer, Count );
					if( Src->IsError() )
					{
						Result = COPY_Fail;
						break;
					}
					Dest->Serialize( Buffer, Count );
					if( Dest->IsError() )
					{
						Result = COPY_Fail;
						break;
					}
					NewPercent = Total * 100 / Size;
					if( Progress && Percent != NewPercent && !Progress->Poll( ( float )NewPercent / 100.f ) )
					{
						Result = COPY_Canceled;
						break;
					}
					Percent = NewPercent;
				}
				delete [] Buffer;
				if( Result == COPY_OK && !Dest->Close() )
				{
					Result = COPY_Fail;
				}
				delete Dest;
				if( Result != COPY_OK )
				{
					Delete( *DestFile );
				}
			}
			if( Result == COPY_OK && !Src->Close() )
			{
				Result = COPY_Fail;
			}
			delete Src;
		}
		if( Progress && Result==COPY_OK && !Progress->Poll( 1.0 ) )
		{
			Result = COPY_Canceled;
		}
	}
	else
	{
		Result = COPY_Canceled;
	}

	return Result;
}

bool FFileManagerGeneric::Delete( const TCHAR* Filename, bool RequireExists, bool EvenReadOnly, bool Quiet )
{
	// Only allow writes to files that are not signed 
	// Except if the file is missing( that way corrupt ini files can be autogenerated by deleting them )
	if( FSHA1::GetFileSHAHash( Filename, NULL ) )
	{
		if (!Quiet)
		{
			UE_LOG( LogFileManager, Log, TEXT( "Can't delete signed game file: %s" ),Filename );
		}
		return false;
	}
	bool bExists = GetLowLevel().FileExists( Filename );
	if( RequireExists && !bExists )
	{
		if (!Quiet)
		{
			UE_LOG( LogFileManager, Warning, TEXT( "Could not delete %s because it doesn't exist." ), Filename );
		}
		return false;
	}
	if( bExists )
	{
		if( EvenReadOnly )
		{
			GetLowLevel().SetReadOnly( Filename, false );
		}
		if( !GetLowLevel().DeleteFile( Filename ) )
		{
			if (!Quiet)
			{
				UE_LOG( LogFileManager, Warning, TEXT( "Error deleting file: %s (Error Code %i)" ), Filename, FPlatformMisc::GetLastError() );
			}
			return false;
		}
	}
	return true;
}

bool FFileManagerGeneric::IsReadOnly( const TCHAR* Filename )
{
	return GetLowLevel().IsReadOnly( Filename );
}

bool FFileManagerGeneric::Move( const TCHAR* Dest, const TCHAR* Src, bool Replace, bool EvenIfReadOnly, bool Attributes, bool bDoNotRetryOrError )
{
	MakeDirectory( *FPaths::GetPath(Dest), true );
	// Retry on failure, unless the file wasn't there anyway.
	if( GetLowLevel().FileExists( Dest ) && !GetLowLevel().DeleteFile( Dest ) && !bDoNotRetryOrError )
	{
		// If the delete failed, throw a warning but retry before we throw an error
		UE_LOG( LogFileManager, Warning, TEXT( "DeleteFile was unable to delete '%s', retrying in .5s..." ), Dest );

		// Wait just a little bit( i.e. a totally arbitrary amount )...
		FPlatformProcess::Sleep( 0.5f );

		// Try again
		if( !GetLowLevel().DeleteFile( Dest ) )
		{
			UE_LOG( LogFileManager, Error, TEXT( "Error deleting file '%s'." ), Dest );
			return false;
		}
		else
		{
			UE_LOG( LogFileManager, Warning, TEXT( "DeleteFile recovered during retry!" ) );
		}		
	}

	if( !GetLowLevel().MoveFile( Dest, Src ) )
	{
		if( bDoNotRetryOrError )
		{
			return false;
		}

		int32 RetryCount = 10;
		bool bSuccess = false;
		while (RetryCount--)
		{
			// If the move failed, throw a warning but retry before we throw an error
			UE_LOG(LogFileManager, Warning, TEXT("MoveFile was unable to move '%s' to '%s', retrying in .5s..."), Src, Dest);

			// Wait just a little bit( i.e. a totally arbitrary amount )...
			FPlatformProcess::Sleep(0.5f);

			// Try again
			bSuccess = GetLowLevel().MoveFile(Dest, Src);
			if (bSuccess)
			{
				UE_LOG(LogFileManager, Warning, TEXT("MoveFile recovered during retry!"));
				break;
			}
		}
		if (!bSuccess)
		{
			UE_LOG( LogFileManager, Error, TEXT( "Error moving file '%s' to '%s'." ), Src, Dest );
			return false;
		}
	}
	return true;
}

bool FFileManagerGeneric::FileExists( const TCHAR* Filename )
{
	return GetLowLevel().FileExists( Filename );
}

bool FFileManagerGeneric::DirectoryExists( const TCHAR* InDirectory )
{
	return GetLowLevel().DirectoryExists( InDirectory );
}

bool FFileManagerGeneric::MakeDirectory( const TCHAR* Path, bool Tree )
{
	if (Tree)
	{
		return GetLowLevel().CreateDirectoryTree(Path);
	}
	else
	{
		return GetLowLevel().CreateDirectory(Path);
	}
}

bool FFileManagerGeneric::DeleteDirectory( const TCHAR* Path, bool RequireExists, bool Tree )
{
	if( Tree )
	{
		return GetLowLevel().DeleteDirectoryRecursively( Path ) || ( !RequireExists && !GetLowLevel().DirectoryExists( Path ) );
	}
	return GetLowLevel().DeleteDirectory( Path ) || ( !RequireExists && !GetLowLevel().DirectoryExists( Path ) );
}

FFileStatData FFileManagerGeneric::GetStatData(const TCHAR* FilenameOrDirectory)
{
	return GetLowLevel().GetStatData(FilenameOrDirectory);
}

void FFileManagerGeneric::FindFiles( TArray<FString>& Result, const TCHAR* InFilename, bool Files, bool Directories )
{
	class FFileMatch : public IPlatformFile::FDirectoryVisitor
	{
	public:
		TArray<FString>& Result;
		FString WildCard;
		bool bFiles;
		bool bDirectories;
		FFileMatch( TArray<FString>& InResult, const FString& InWildCard, bool bInFiles, bool bInDirectories )
			: Result( InResult )
			, WildCard( InWildCard )
			, bFiles( bInFiles )
			, bDirectories( bInDirectories )
		{
		}
		virtual bool Visit( const TCHAR* FilenameOrDirectory, bool bIsDirectory )
		{
			if (((bIsDirectory && bDirectories) || (!bIsDirectory && bFiles))
				&& FPaths::GetCleanFilename(FilenameOrDirectory).MatchesWildcard(WildCard))
			{
				new( Result ) FString( FPaths::GetCleanFilename(FilenameOrDirectory) );
			}
			return true;
		}
	};
	FString Filename( InFilename );
	FPaths::NormalizeFilename( Filename );
	const FString CleanFilename = FPaths::GetCleanFilename(Filename);
	const bool bFindAllFiles = CleanFilename == TEXT("*") || CleanFilename == TEXT("*.*");
	FFileMatch FileMatch( Result, bFindAllFiles ? TEXT("*") : CleanFilename, Files, Directories );
	GetLowLevel().IterateDirectory( *FPaths::GetPath(Filename), FileMatch );
}

void FFileManagerGeneric::FindFiles(TArray<FString>& FoundFiles, const TCHAR* Directory, const TCHAR* FileExtension)
{
	if (!Directory)
	{
		return;
	}

	FString RootDir(Directory);
	FString ExtStr = (FileExtension != nullptr) ? FString(FileExtension) : "";

	// No Directory?
	if (RootDir.Len() < 1)
	{
		return;
	}

	FPaths::NormalizeDirectoryName(RootDir);

	// Don't modify the ExtStr if the user supplied the form "*.EXT" or "*" or "*.*" or "Name.*"
	if (!ExtStr.Contains(TEXT("*")))
	{
		if (ExtStr == "")
		{
			ExtStr = "*.*";
		}
		else
		{
			//Complete the supplied extension with * or *. to yield "*.EXT"
			ExtStr = (ExtStr.Left(1) == ".") ? "*" + ExtStr : "*." + ExtStr;
		}
	}

	// Create the full filter, which is "Directory/*.EXT".
	FString FinalPath = RootDir + "/" + ExtStr;
	FindFiles(FoundFiles, *FinalPath, true, false);
}

bool FFileManagerGeneric::IterateDirectory(const TCHAR* Directory, IPlatformFile::FDirectoryVisitor& Visitor)
{
	return GetLowLevel().IterateDirectory( Directory, Visitor );
}

bool FFileManagerGeneric::IterateDirectoryRecursively(const TCHAR* Directory, IPlatformFile::FDirectoryVisitor& Visitor)
{
	return GetLowLevel().IterateDirectoryRecursively( Directory, Visitor );
}

bool FFileManagerGeneric::IterateDirectoryStat(const TCHAR* Directory, IPlatformFile::FDirectoryStatVisitor& Visitor)
{
	return GetLowLevel().IterateDirectoryStat( Directory, Visitor );
}

bool FFileManagerGeneric::IterateDirectoryStatRecursively(const TCHAR* Directory, IPlatformFile::FDirectoryStatVisitor& Visitor)
{
	return GetLowLevel().IterateDirectoryStatRecursively( Directory, Visitor );
}

double FFileManagerGeneric::GetFileAgeSeconds( const TCHAR* Filename )
{
	// make sure it exists
	if (!GetLowLevel().FileExists(Filename))
	{
		return -1.0;
	}
	// get difference in time between now (UTC) and the filetime
	FTimespan Age = FDateTime::UtcNow() - GetTimeStamp(Filename);
	return Age.GetTotalSeconds();
}

FDateTime FFileManagerGeneric::GetTimeStamp(const TCHAR* Filename)
{
	// ask low level for timestamp
	return GetLowLevel().GetTimeStamp(Filename);
}

void FFileManagerGeneric::GetTimeStampPair(const TCHAR* PathA, const TCHAR* PathB, FDateTime& OutTimeStampA, FDateTime& OutTimeStampB)
{
	GetLowLevel().GetTimeStampPair(PathA, PathB, OutTimeStampA, OutTimeStampB);
}

bool FFileManagerGeneric::SetTimeStamp(const TCHAR* Filename, FDateTime DateTime)
{
	// if the file doesn't exist, fail
	if (!GetLowLevel().FileExists(Filename))
	{
		return false;
	}

	GetLowLevel().SetTimeStamp(Filename, DateTime);
	return true;
}

FDateTime FFileManagerGeneric::GetAccessTimeStamp( const TCHAR* Filename )
{
	// ask low level for timestamp
	return GetLowLevel().GetAccessTimeStamp(Filename);
}

FString FFileManagerGeneric::GetFilenameOnDisk(const TCHAR* Filename)
{
	return GetLowLevel().GetFilenameOnDisk(Filename);
}

FString FFileManagerGeneric::DefaultConvertToRelativePath( const TCHAR* Filename )
{
	//default to the full absolute path of this file
	FString RelativePath( Filename );
	FPaths::NormalizeFilename(RelativePath);

	// See whether it is a relative path.
	FString RootDirectory( FPlatformMisc::RootDir() );
	FPaths::NormalizeFilename(RootDirectory);

	//the default relative directory it to the app root which is 3 directories up from the starting directory
	int32 NumberOfDirectoriesToGoUp = 3;

	//temp holder for current position of the slash
	int32 CurrentSlashPosition;

	//while we haven't run out of parent directories until we which a drive name
	while( ( CurrentSlashPosition = RootDirectory.Find( TEXT("/"), ESearchCase::CaseSensitive, ESearchDir::FromEnd ) ) != INDEX_NONE )
	{
		if( RelativePath.StartsWith( RootDirectory ) )
		{
			FString BinariesDir = FString(FPlatformProcess::BaseDir());
			FPaths::MakePathRelativeTo( RelativePath, *BinariesDir );
			break;
		}
		int32 PositionOfNextSlash = RootDirectory.Find( TEXT("/"), ESearchCase::CaseSensitive, ESearchDir::FromEnd, CurrentSlashPosition );
		//if there is another slash to find
		if( PositionOfNextSlash != INDEX_NONE )
		{
			//move up a directory and on an extra .. TEXT("/")
			// the +1 from "InStr" moves to include the "\" at the end of the directory name
			NumberOfDirectoriesToGoUp++;
			RootDirectory = RootDirectory.Left( PositionOfNextSlash + 1 );
		}
		else
		{
			RootDirectory.Empty();
		}
	}
	return RelativePath;
}

FString FFileManagerGeneric::ConvertToRelativePath( const TCHAR* Filename )
{
	return DefaultConvertToRelativePath(Filename);
}

FString FFileManagerGeneric::ConvertToAbsolutePathForExternalAppForRead( const TCHAR* AbsolutePath )
{
	return GetLowLevel().ConvertToAbsolutePathForExternalAppForRead( AbsolutePath );
}

FString FFileManagerGeneric::ConvertToAbsolutePathForExternalAppForWrite( const TCHAR* AbsolutePath )
{
	return GetLowLevel().ConvertToAbsolutePathForExternalAppForWrite( AbsolutePath );
}

void FFileManagerGeneric::FindFilesRecursive( TArray<FString>& FileNames, const TCHAR* StartDirectory, const TCHAR* Filename, bool Files, bool Directories, bool bClearFileNames)
{
	if( bClearFileNames )
	{
		FileNames.Empty();
	}
	FindFilesRecursiveInternal(FileNames, StartDirectory, Filename, Files, Directories);
}

void FFileManagerGeneric::FindFilesRecursiveInternal( TArray<FString>& FileNames, const TCHAR* StartDirectory, const TCHAR* Filename, bool Files, bool Directories)
{
	FString CurrentSearch = FString(StartDirectory) / Filename;
	TArray<FString> Result;
	FindFiles(Result, *CurrentSearch, Files, Directories);

	for (int32 i=0; i<Result.Num(); i++)
	{
		FileNames.Add(FString(StartDirectory) / Result[i]);
	}

	TArray<FString> SubDirs;
	FString RecursiveDirSearch = FString(StartDirectory) / TEXT("*");
	FindFiles(SubDirs, *RecursiveDirSearch, false, true);

	for (int32 SubDirIdx=0; SubDirIdx<SubDirs.Num(); SubDirIdx++)
	{
		FString SubDir = FString(StartDirectory) / SubDirs[SubDirIdx];
		FindFilesRecursiveInternal(FileNames, *SubDir, Filename, Files, Directories);
	}
}

FArchiveFileReaderGeneric::FArchiveFileReaderGeneric( IFileHandle* InHandle, const TCHAR* InFilename, int64 InSize )
	: Filename( InFilename )
	, Size( InSize )
	, Pos( 0 )
	, BufferBase( 0 )
	, BufferCount( 0 )
	, Handle( InHandle )
{
	ArIsLoading = ArIsPersistent = true;
}


void FArchiveFileReaderGeneric::Seek( int64 InPos )
{
	checkf(InPos >= 0, TEXT("Attempted to seek to a negative location (%lld/%lld), file: %s. The file is most likely corrupt."), InPos, Size, *Filename);
	checkf(InPos <= Size, TEXT("Attempted to seek past the end of file (%lld/%lld), file: %s. The file is most likely corrupt."), InPos, Size, *Filename);
	if( !SeekLowLevel( InPos ) )
	{
		TCHAR ErrorBuffer[1024];
		ArIsError = true;
		UE_LOG(LogFileManager, Error, TEXT("SetFilePointer on %s Failed %lld/%lld: %lld %s"), *Filename, InPos, Size, Pos, FPlatformMisc::GetSystemErrorMessage(ErrorBuffer, 1024, 0));
	}
	Pos         = InPos;
	BufferBase  = Pos;
	BufferCount = 0;
}

FArchiveFileReaderGeneric::~FArchiveFileReaderGeneric()
{
	Close();
}

void FArchiveFileReaderGeneric::ReadLowLevel( uint8* Dest, int64 CountToRead, int64& OutBytesRead )
{
	if( Handle->Read( Dest, CountToRead ) )
	{
		OutBytesRead = CountToRead;
	}
	else
	{
		OutBytesRead = 0;
	}
}

bool FArchiveFileReaderGeneric::SeekLowLevel( int64 InPos )
{
	return Handle->Seek( InPos );
}

void FArchiveFileReaderGeneric::CloseLowLevel()
{
	Handle.Reset();
}

bool FArchiveFileReaderGeneric::Close()
{
	CloseLowLevel();
	return !ArIsError;
}

bool FArchiveFileReaderGeneric::InternalPrecache( int64 PrecacheOffset, int64 PrecacheSize )
{
	// Only precache at current position and avoid work if precaching same offset twice.
	if( Pos == PrecacheOffset &&( !BufferBase || !BufferCount || BufferBase != Pos ) )
	{
		BufferBase = Pos;
		BufferCount = FMath::Min( FMath::Min( PrecacheSize,( int64 )( ARRAY_COUNT( Buffer ) -( Pos&( ARRAY_COUNT( Buffer )-1 ) ) ) ), Size-Pos );
		BufferCount = FMath::Max( BufferCount, 0LL ); // clamp to 0
		int64 Count = 0;

		{
			if (BufferCount > ARRAY_COUNT( Buffer ) || BufferCount <= 0)
			{
				UE_LOG( LogFileManager, Error, TEXT("Invalid BufferCount=%lld while reading %s. File is most likely corrupted. Please verify your installation. Pos=%lld, Size=%lld, PrecacheSize=%lld, PrecacheOffset=%lld"),
					BufferCount, *Filename, Pos, Size, PrecacheSize, PrecacheOffset );

				return false;
			}

			ReadLowLevel( Buffer, BufferCount, Count );
		}

		if( Count!=BufferCount )
		{
			TCHAR ErrorBuffer[1024];
			ArIsError = true;
			UE_LOG( LogFileManager, Warning, TEXT( "ReadFile failed: Count=%lld BufferCount=%lld Error=%s" ), Count, BufferCount, FPlatformMisc::GetSystemErrorMessage( ErrorBuffer, 1024, 0 ) );
		}
	}
	return true;
}

void FArchiveFileReaderGeneric::Serialize( void* V, int64 Length )
{
	while( Length>0 )
	{
		int64 Copy = FMath::Min( Length, BufferBase+BufferCount-Pos );
		if( Copy<=0 )
		{
			if( Length >= ARRAY_COUNT( Buffer ) )
			{
				int64 Count=0;
				{
					ReadLowLevel(( uint8* )V, Length, Count );
				}
				if( Count!=Length )
				{
					TCHAR ErrorBuffer[1024];
					ArIsError = true;
					UE_LOG( LogFileManager, Warning, TEXT( "ReadFile failed: Count=%lld Length=%lld Error=%s for file %s" ), 
						Count, Length, FPlatformMisc::GetSystemErrorMessage( ErrorBuffer, 1024, 0 ), *Filename );
				}
				Pos += Length;
				return;
			}
			if (!InternalPrecache(Pos, MAX_int32))
			{
				ArIsError = true;
				UE_LOG( LogFileManager, Warning, TEXT( "ReadFile failed during precaching for file %s" ),*Filename );
				return;
			}
			Copy = FMath::Min( Length, BufferBase+BufferCount-Pos );
			if( Copy<=0 )
			{
				ArIsError = true;
				UE_LOG( LogFileManager, Error, TEXT( "ReadFile beyond EOF %lld+%lld/%lld for file %s" ), 
					Pos, Length, Size, *Filename );
			}
			if( ArIsError )
			{
				return;
			}
		}
		FMemory::Memcpy( V, Buffer+Pos-BufferBase, Copy );
		Pos       += Copy;
		Length    -= Copy;
		V          =( uint8* )V + Copy;
	}
}

FArchiveFileWriterGeneric::FArchiveFileWriterGeneric( IFileHandle* InHandle, const TCHAR* InFilename, int64 InPos )
	: Filename( InFilename )
	, Pos( InPos )
	, BufferCount( 0 )
	, Handle( InHandle )
	, bLoggingError( false )
{
	ArIsSaving = ArIsPersistent = true;
}

FArchiveFileWriterGeneric::~FArchiveFileWriterGeneric()
{
	Close();
}

bool FArchiveFileWriterGeneric::CloseLowLevel()
{
	Handle.Reset();
	return true;
}

bool FArchiveFileWriterGeneric::SeekLowLevel( int64 InPos )
{
	return Handle->Seek( InPos );
}

int64 FArchiveFileWriterGeneric::TotalSize()
{
	// Make sure that all data is written before looking at file size.
	Flush();
	return Handle->Size();
}

bool FArchiveFileWriterGeneric::WriteLowLevel( const uint8* Src, int64 CountToWrite )
{
	return Handle->Write( Src, CountToWrite );
}

void FArchiveFileWriterGeneric::Seek( int64 InPos )
{
	Flush();
	if( !SeekLowLevel( InPos ) )
	{
		ArIsError = true;
		LogWriteError(TEXT("Error seeking file"));
	}
	Pos = InPos;
}

bool FArchiveFileWriterGeneric::Close()
{
	Flush();
	if( !CloseLowLevel() )
	{
		ArIsError = true;
		LogWriteError(TEXT("Error closing file"));
	}
	return !ArIsError;
}

void FArchiveFileWriterGeneric::Serialize( void* V, int64 Length )
{
	Pos += Length;
	if ( Length >= ARRAY_COUNT(Buffer) )
	{
		Flush();
		if( !WriteLowLevel( (uint8*)V, Length ) )
		{
			ArIsError = true;
			LogWriteError(TEXT("Error writing to file"));
		}
	}
	else
	{
		int64 Copy;
		while( Length >( Copy=ARRAY_COUNT( Buffer )-BufferCount ) )
		{
			FMemory::Memcpy( Buffer+BufferCount, V, Copy );
			BufferCount += Copy;
			check( BufferCount <= ARRAY_COUNT( Buffer ) && BufferCount >= 0 );
			Length      -= Copy;
			V            =( uint8* )V + Copy;
			Flush();
		}
		if( Length )
		{
			FMemory::Memcpy( Buffer+BufferCount, V, Length );
			BufferCount += Length;
			check( BufferCount <= ARRAY_COUNT( Buffer ) && BufferCount >= 0 );
		}
	}
}

void FArchiveFileWriterGeneric::Flush()
{
	if( BufferCount )
	{
		check( BufferCount <= ARRAY_COUNT( Buffer ) && BufferCount > 0 );
		if( !WriteLowLevel( Buffer, BufferCount ) )
		{
			ArIsError = true;
			LogWriteError(TEXT("Error flushing file"));
		}
		BufferCount = 0;
	}
}

void FArchiveFileWriterGeneric::LogWriteError(const TCHAR* Message)
{
	// Prevent re-entry if logging causes another log error leading to a stack overflow
	if (!bLoggingError)
	{
		bLoggingError = true;
		TCHAR ErrorBuffer[1024];
		UE_LOG(LogFileManager, Error, TEXT("%s: %s (%s)"), Message, *Filename, FPlatformMisc::GetSystemErrorMessage(ErrorBuffer, 1024, 0));
		bLoggingError = false;
	}
}
//---


IFileManager& IFileManager::Get()
{
	static TUniquePtr<FFileManagerGeneric> AutoDestroySingleton;
	if( !AutoDestroySingleton )
	{
		AutoDestroySingleton = MakeUnique<FFileManagerGeneric>();
	}
	return *AutoDestroySingleton;
}

