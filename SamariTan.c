// "Design" "doc":
// - adding files
// - deleting files
// - creating new disk images
// - extract deleted files -because tIn insisted-
// - monitoring directory and if it changes sync the differences with the image
// - "Make me a 800KB floppy with the following files on"
// - Supply disk geometry for new images, or use the default expending strategy

#ifdef _MSC_VER
#define VC_EXTRALEAN
#define WIN32_LEAN_AND_MEAN
#define _CRT_SECURE_NO_WARNINGS
#include <windows.h>
#include "Shlwapi.h"
#endif

#include <stdlib.h>
#include <stdio.h>

#if defined(_WIN32) && !defined(__MINGW32__)
#define DIR_SEPARATOR_STRING "\\"
#define FOPEN_S(a,b,c) fopen_s(&a,b,c)
#else
#include <unistd.h>
#define DIR_SEPARATOR_STRING "/"
#if !defined(__MINGW32__)
#define __stdcall
#endif
#define sprintf_s(a,b,...) sprintf(a,__VA_ARGS__)
#define strcpy_s(a,b,c) strcpy(a,c)
#define FOPEN_S(a,b,c) a=fopen(b,c)
#define _strcmpi strcasecmp
typedef char *LPCSTR;
#define _stat stat
#define _ftelli64 ftello
#include <signal.h>
#define DebugBreak() raise(SIGTRAP);
#include <ctype.h>
#define TRUE true
#define FALSE false
#define BOOL bool
#include <stdbool.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <string.h>
#include <errno.h>
#endif

#include "wcxhead.h"
#include "dosfs-1.03/dosfs.h"
#include "jacknife.h"

extern int Pack(char *PackedFile, char *SubPath, char *SrcPath, char *AddList, int Flags, NEW_DISK_GEOMETRY *geometry);
extern int Delete(char *PackedFile, char *DeleteList);
extern int install_bootsector(char *image_file, char *bootsector_filename);
extern int add_volume_label(char *image_file, char *volume_name);

typedef enum
{
    ST_NONE,
    ST_CREATE,
    ST_ADD,
    ST_DELETE,
    ST_EXTRACT,
    ST_UNDELETE,
    ST_MONITOR
} ST_MODES;

char *get_human_readable_error_string(return_codes code)
{
    switch (code)
    {
        case J_OK:
            return "OK";
        case J_EOF:
            return "EOF";
        case J_WRITE_PROTECTED:
            return "Write protected";
        case J_FILE_NOT_FOUND:
            return "File not found";
        case J_PATHNAME_TOO_LARGE:
            return "Pathname too large";
        case J_MUST_ALLOCTE_NEW_ITEM:
            return "Must allocte new item";
        case J_ITEM_IS_DIRECTORY:
            return "Item is directory";
        case J_MALLOC_ERROR:
            return "Malloc error";
        case J_INVALID_MSA:
            return "Invalid msa";
        case J_INVALID_DIM:
            return "Invalid dim";
        case J_INVALID_HARD_DISK_IMAGE:
            return "Invalid hard disk image";
        //case J_FAIL:
        //    return "Fail";
        case J_BOOTSECTOR_FILE_NOT_FOUND:
            return "Bootsector file not found";
        case J_INVALID_BOOTSECTOR_SIZE:
            return "Invalid bootsector size";
        case J_READ_ERROR:
            return "Read error";
        case J_CANNOT_CREATE_FILE:
            return "Cannot create file";
        case J_MEMORY_ALLOCATION_ERROR:
            return "Memory allocation error";
        case J_OUT_OF_DISK_SPACE:
            return "Out of disk space";
    }
}

BOOL check_if_pathname_exists(char *pathname)
{
#if (defined(_WIN32) || defined(_WIN64)) && !defined(__MINGW32__)
    BOOL test = PathFileExistsA(pathname);
#else
    struct stat path_info;
    int test = stat(pathname, &path_info);
    test = !((test == -1) && (errno == ENOENT));
#endif
    return (BOOL)test;
}

BOOL check_if_directory(char *pathname)
{
#if (defined(WIN32) || defined(WIN64)) && !defined(__MINGW32__)
    BOOL test = GetFileAttributesA(pathname) & FILE_ATTRIBUTE_DIRECTORY;
#else
    struct stat path_info;
    int test = stat(pathname, &path_info);
    if (test >= 0) test = ((path_info.st_mode & S_IFMT) == S_IFDIR);
#endif
    return (BOOL)test;
}

void print_usage()
{
    printf("samaritan -c <archive name> <extra parameters> <list of files to add>: This will create an image file with the name *archive name* and will proceed in adding all files in the *list of files to add*. Note that the *archive name* must not exist. For the time being do *not* supply absolute paths. The only properly tested mode is to supply files relative to the current path, as they will be added with all the subfolders they might be in. Also note that starting a relative path with a .. to move up one diretcory will most likely result in an error.\n"
    "samaritan -a <archive name> <extra parameters> <list of files to add>: This will open an image file with the name *archive name* and will proceed in adding all files in the *list of files to add*. For the time being do *not* supply absolute paths. The only properly tested mode is to supply files relative to the current path, as they will be added with all the subfolders they might be in. Also note that starting a relative path with a .. to move up one diretcory will most likely result in an error."
    "samaritan -d <archive name> <list of files to delete>`: This will attempt to delete all files in <list of files to delete> from the archive <archive name>\n"
    "\n"
    "<extra parameters> are the following:\n"
    "\n"
    " -b <bootsector file>: Installs an up to 480 bytes bootsector\n"
    " -l <volume name>: Adds a volume name\n"
    " -t <number>: Specify number of tracks for custom disk geometry\n"
    " -s <number>: Specify number of sectors for custom disk geometry\n"
    " -i <number>: Specify number of sides for custom disk geometry\n");
}

int main(int argc, char **argv)
{
    if (argc < 4)
    {
        print_usage();
        return -1;
    }

    // Eat the program filename argument
    argv++;
    argc--;

    int bootsector_install = 0;
    int volume_label = 0;
    int volume_label_index = 0;
    int bootsector_name_index = 0;
    int filenames_start_index = 0;
    int custom_geometry = 0;
    NEW_DISK_GEOMETRY geometry =
    {
        .tracks = -1,
        .sectors = -1,
        .sides = -1
    };
    int i = 0;

    ST_MODES mode = ST_NONE;

    while (i < argc && argv[i][0] == '-')
    {
        if (argv[i][1] == 'c')
        {
            mode = ST_CREATE;
            i++;
        }
        else if (argv[i][1] == 'd')
        {
            mode = ST_DELETE;
            i++;
        }
        else if (argv[i][1] == 'a')
        {
            mode = ST_ADD;
            i++;
        }
        else if (argv[i][1] == 'b')
        {
            if (i + 1 >= argc)
            {
                printf("No filename supplied for -b\n");
                return -1;
            }
            bootsector_name_index = i + 1;
            bootsector_install = 1;
            i++;                        // Skip parameter 'b' and bootsector filename
        }
        else if (argv[i][1] == 'l')
        {
            if (i + 1 >= argc)
            {
                printf("No volume name supplied for -l\n");
                return -1;
            }
            volume_label = 1;
            volume_label_index = i + 1;
            i++;                        // Skip parameter 'l' and disk label name
        }
        else if (argv[i][1] == 't')
        {
            if (mode != ST_CREATE)
            {
                printf("-t requires -c\n");
                return -1;
            }
            if (i + 1 >= argc)
            {
                printf("No tracks count supplied for -t\n");
                return -1;
            }
            custom_geometry = 1;
            geometry.tracks = atoi(argv[i + 1]);
            i++;
        }
        else if (argv[i][1] == 's')
        {
            if (mode != ST_CREATE)
            {
                printf("-s requires -c\n");
                return -1;
            }
            if (i + 1 >= argc)
            {
                printf("No sector count supplied for -s\n");
                return -1;
            }
            custom_geometry = 1;
            geometry.sectors = atoi(argv[i + 1]);
            i++;
        }
        else if (argv[i][1] == 'i')
        {
            if (mode != ST_CREATE)
            {
                printf("-i requires -c\n");
                return -1;
            }
            if (i + 1 >= argc)
            {
                printf("No side count supplied for -i\n");
                return -1;
            }
            custom_geometry = 1;
            geometry.sides = atoi(argv[i + 1]);
            i++;
        }
        else if (argv[i][1] == 'h')
        {
            print_usage();
            return 0;
        }

        i++;
    }
    filenames_start_index = i;

    if (custom_geometry && (geometry.sides==-1 || geometry.tracks==-1||geometry.sectors==-1))
    {
        printf("Incomplete disk geometry - ");
        if (geometry.sides == -1)
        {
            printf("sides ");
        }
        if (geometry.sectors == -1)
        {
            printf("sectors ");
        }
        if (geometry.tracks == -1)
        {
            printf("tracks ");
        }
        printf("not specified\n");
        exit(-1);
    }

    char tc_file_listing[4096] = { 0 }; // TODO either make this a resizable array, or make 2 passes scanning filenames (the first to count characters)
    switch (mode)
    {
    case ST_CREATE:
    {
        if (check_if_pathname_exists(argv[1]))
        {
            printf("image filename exists - aborting\n");
            return -1;
        }
        // And we fallthrough to the next state
    }
    case ST_ADD:
    {
        // Populate file listing
        int i;
        char *current_file = tc_file_listing;
        for (i = filenames_start_index; i < argc; i++)
        {
            // TODO: disallow absolute filenames on windows (i.e. strip off "C:")
            if (!check_if_pathname_exists(argv[i]))
            {
                printf("file %s doesn't exist\n",argv[i]);
                return -1;
            }

            // Check if it's a file
            if (!check_if_directory(argv[i]))
            {
                // It's a file, add it to the list
                // If it's a complex pathname, i.e. "a\b\c\file" then add entries like
                // "a\", "a\b\", "a\b\c\", "a\b\c\file", so that the subfolders can be created
                int j;
                for (j = 0; j < strlen(argv[i]); j++)
                {
                    if (j != 0 && argv[i][j] == DIR_SEPARATOR)
                    {
                        memcpy(current_file, argv[i], j + 1);
                        current_file += j + 1;
                        *current_file++ = 0;
                    }
                }
                strcat(current_file, argv[i]);
                current_file += strlen(current_file) + 1; // Move past the filename and the 0 terminator
            }
            else
            {
                // It's a directory, we have to scan all the things and then add them to the list
                printf("add directory recursively to the list - TODO: better error message\n");
            }
        }
        *current_file = 0; // Add a second 0 terminator to indicate end of list

        NEW_DISK_GEOMETRY *pass_geometry = 0;
        if (custom_geometry)
            pass_geometry = &geometry;

        int ret = Pack(argv[1], "", "", tc_file_listing, PK_PACK_SAVE_PATHS, pass_geometry);
        if (ret)
        {
            printf("Pack failed: %s\n", get_human_readable_error_string(ret));
            return -1;
        }

        ret = add_volume_label(argv[1], argv[volume_label_index]);
        if (ret)
        {
            printf("Volume label install fail: %s \n", get_human_readable_error_string(ret));
            return -1;
        }

        break;
    }
    case ST_DELETE:
    {
        // Populate file listing
        int i;
        char *current_file = tc_file_listing;
        for (i = filenames_start_index; i < argc; i++)
        {
            strcat(current_file, argv[i]);
            current_file += strlen(current_file) + 1; // Move past the filename and the 0 terminator
        }
        *current_file = 0; // Add a second 0 terminator to indicate end of list
        int ret = Delete(argv[1], tc_file_listing);
        if (ret)
        {
            printf("Delete fail: %s\n",get_human_readable_error_string(ret));
        }

        break;
    }
    default:
        printf("unknown mode\n");
        print_usage();
        return -1;
    }

    if (bootsector_install)
    {
        printf("Installing bootsector %s\n", argv[bootsector_name_index]);
        int ret = install_bootsector(argv[1], argv[bootsector_name_index]);
        if (ret)
        {
            printf("Error, return code: %s\n", get_human_readable_error_string(ret));
            return -1;
        }
    }

    return 0;
}