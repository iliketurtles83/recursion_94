#include "resources.h"
#include "raylib.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOGDI
#define NOUSER
#include <windows.h>
typedef struct ResourceItem { const char *relative; int id; } ResourceItem;
#include "resources_catalog.h"

static char extractedRoot[MAX_PATH];
static bool ownsRoot;
static bool resource_path(char *out,size_t capacity,const char *root,const char *relative)
{
    int count=snprintf(out,capacity,"%s/%s",root,relative);
    return count>=0&&(size_t)count<capacity;
}
static void cleanup_resources(void)
{
    if(!ownsRoot)return;
    // Only remove files listed in our embedded manifest and our own unique dir.
    char path[MAX_PATH];
    for(size_t i=0;i<sizeof(resourceItems)/sizeof(resourceItems[0]);i++)
        if(resource_path(path,sizeof(path),extractedRoot,resourceItems[i].relative))DeleteFileA(path);
    const char *directories[]={"shaders","presets","assets"};
    for(size_t i=0;i<3;i++)if(resource_path(path,sizeof(path),extractedRoot,directories[i]))RemoveDirectoryA(path);
    RemoveDirectoryA(extractedRoot);ownsRoot=false;
}
static bool complete_resources(const char *root)
{
    char path[1024];
    for(size_t i=0;i<sizeof(resourceItems)/sizeof(resourceItems[0]);i++)
        if(!resource_path(path,sizeof(path),root,resourceItems[i].relative)||!FileExists(path))return false;
    return true;
}
#endif

bool resources_prepare(char *root,size_t capacity)
{
    const char *adjacent=GetApplicationDirectory();
    if(strlen(adjacent)>=capacity)return false;
    strcpy(root,adjacent);
#ifdef _WIN32
    if(complete_resources(root))return true;
    char temporary[MAX_PATH],path[MAX_PATH];
    DWORD length=GetTempPathA(sizeof(temporary),temporary);
    if(!length||length>=sizeof(temporary)||!GetTempFileNameA(temporary,"DRN",0,extractedRoot))return false;
    if(!DeleteFileA(extractedRoot)||!CreateDirectoryA(extractedRoot,NULL))return false;
    ownsRoot=true;atexit(cleanup_resources);
    const char *directories[]={"shaders","presets","assets"};
    for(size_t i=0;i<3;i++)
        if(!resource_path(path,sizeof(path),extractedRoot,directories[i])||!CreateDirectoryA(path,NULL))goto failed;
    HMODULE executable=GetModuleHandleA(NULL);
    for(size_t i=0;i<sizeof(resourceItems)/sizeof(resourceItems[0]);i++) {
        // Integer resource identifiers; type 10 is Win32 RCDATA. NOUSER keeps
        // Windows drawing names from conflicting with raylib's public API.
        HRSRC entry=FindResourceA(executable,(LPCSTR)(ULONG_PTR)resourceItems[i].id,(LPCSTR)(ULONG_PTR)10);
        if(!entry)goto failed;
        DWORD bytes=SizeofResource(executable,entry);
        HGLOBAL handle=LoadResource(executable,entry);
        const void *data=handle?LockResource(handle):NULL;
        if(!data||!bytes||!resource_path(path,sizeof(path),extractedRoot,resourceItems[i].relative))goto failed;
        FILE *file=fopen(path,"wb");
        if(!file)goto failed;
        bool wrote=fwrite(data,1,bytes,file)==bytes;
        if(fclose(file)!=0)wrote=false;
        if(!wrote)goto failed;
    }
    if(strlen(extractedRoot)>=capacity)goto failed;
    strcpy(root,extractedRoot);
    printf("resources: using embedded shaders, presets, texture and font\n");
    return true;
failed:
    cleanup_resources();
    fprintf(stderr,"resources: could not unpack embedded game assets\n");
    return false;
#else
    char path[1024];
    snprintf(path,sizeof(path),"%s/shaders/mission.vs",root);
    return FileExists(path);
#endif
}
