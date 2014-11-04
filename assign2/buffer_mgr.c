#include <stdlib.h>
#include <stdio.h>
#include <limits.h>

#include "storage_mgr.h"
#include "buffer_mgr.h"

#define RC_ERROR -1;

typedef struct PageInfo {
    int isactive;
    int readcount;
    int writecount;
    bool fixcount;
    int isdirty;
    int priority;
    BM_PageHandle *ph;
} PageInfo;

int globalread = 0;
int globalwrite = 0;

int globalpriority = 0;

void printpri(BM_BufferPool *const bm) {
    struct PageInfo *pages;
    pages = bm->mgmtData;

    PageNumber *frameContent;
    int *pr;

    for (int i = 0; i < bm->numPages; i++)
        printf("[%d]", pages[i].priority);
    printf("\n");
}

// Buffer Manager Interface Pool Handling
RC initBufferPool(BM_BufferPool *const bm, const char *const pageFileName, const int numPages, ReplacementStrategy strategy, void *stratData) {

    // Check file
    SM_FileHandle fh;
    if (openPageFile(pageFileName, &fh) != RC_OK) {
        return RC_ERROR;
    }
    closePageFile(&fh);

    // initialize BufferPool
    struct PageInfo *pages;
    pages = (PageInfo*) malloc (sizeof(PageInfo) * numPages);

    for (int i = 0; i < numPages; i++) {
        pages[i].isactive = 0;
        pages[i].readcount = 0;
        pages[i].writecount = 0;
        pages[i].fixcount = 0;
        pages[i].isdirty = false;
        pages[i].priority = -1;
        pages[i].ph = NULL;
    }

    bm->pageFile = pageFileName;
    bm->numPages = numPages;
    bm->strategy = strategy;
    bm->mgmtData = pages;

    globalread = 0;
    globalwrite = 0;

    // printPoolContent(bm);

    return RC_OK;
}

RC shutdownBufferPool(BM_BufferPool *const bm) {
    // make sure every page is successfuly write to file
    // destory every variable
    return RC_OK;
}

RC forceFlushPool(BM_BufferPool *const bm) {
    // force write every page to file
    struct PageInfo *pages;
    pages = bm->mgmtData;

    for (int i = 0; i < bm->numPages; i++) {
        if (pages[i].isactive == 1 && pages[i].isdirty == true) {
            pages[i].isdirty = false;
        }
    }

    return RC_OK;
}

// Buffer Manager Interface Access Pages
RC markDirty(BM_BufferPool *const bm, BM_PageHandle *const page) {
    // mark the page dirty
    // printf("markdirty %d\n", page->pageNum);

    struct PageInfo *pages;
    pages = bm->mgmtData;

    int foundpoolposition = -1;

    for (int i = 0; i < bm->numPages; i++) {
        if (pages[i].isactive == 1 && pages[i].ph->pageNum == page->pageNum) {
            // printf("found: %d\n", i);
            foundpoolposition = i;
            break;
        }
    }

    if (foundpoolposition == -1) {
        return NO_PAGE;
    }

    pages[foundpoolposition].isdirty = true;

    // printPoolContent(bm);
    // printpri(bm);
    // printf("\n");
    return RC_OK;
}

RC unpinPage(BM_BufferPool *const bm, BM_PageHandle *const page) {
    // if the page is dirty, write it on file
    // unpin the page
    // printf("unpin %d\n", page->pageNum);

    struct PageInfo *pages;
    pages = bm->mgmtData;

    int foundpoolposition = -1;

    for (int i = 0; i < bm->numPages; i++) {
        if (pages[i].isactive == 1 && pages[i].ph->pageNum == page->pageNum) {
            // printf("found: %d\n", i);
            foundpoolposition = i;
            break;
        }
    }

    if (foundpoolposition == -1) {
        return NO_PAGE;
    }

    if (pages[foundpoolposition].isdirty) {
        SM_FileHandle fh;
        openPageFile(bm->pageFile, &fh);
        writeBlock(page->pageNum, &fh, page->data);
        closePageFile(&fh);
        pages[foundpoolposition].writecount++;
        globalwrite++;
    }

    pages[foundpoolposition].fixcount--;

    // printPoolContent(bm);
    // printpri(bm);
    // printf("\n");
    return RC_OK;
}

RC forcePage(BM_BufferPool *const bm, BM_PageHandle *const page) {
    // force write the page to file
    return RC_OK;
}

RC pinPage(BM_BufferPool *const bm, BM_PageHandle *const page, const PageNumber pageNum) {
    // read page to buffer, pin it

    // printf("pin %d\n", pageNum);
    // printPoolContent(bm);
    // printpri(bm);

    struct PageInfo *pages;
    pages = bm->mgmtData;

    // check if page is already in pool OR pool is not full 
    int foundpoolposition = -1;
    for (int i = 0; i < bm->numPages; i++) {
        // if found a blank pool
        if (pages[i].isactive == 0) {
            // printf("found blank\n");
            foundpoolposition = i;
            break;
        }
        // if found match pool
        if (pages[i].isactive == 1 && pages[i].ph->pageNum == pageNum) {
            // printf("found match\n");
            foundpoolposition = i;
            break;
        }
    }

    // if pool is full, find out a place to replace
    int minpriority = INT_MAX;
    if (foundpoolposition == -1) {
        for (int i = 0; i < bm->numPages; i++) {
            if (pages[i].fixcount == 0 && pages[i].priority < minpriority) {
                minpriority = pages[i].priority;
                foundpoolposition = i;
            }
        }

    }

    // if still can't find out a place
    // the pool is full and all page are pined
    if (foundpoolposition == -1) {
        printf("ERROR! PagePool is full.\n");
        return RC_ERROR;
    }

    SM_FileHandle fh;
    openPageFile(bm->pageFile, &fh);

    // printf("found: %d\n", foundpoolposition);

    if (pages[foundpoolposition].isactive == 1 && pages[foundpoolposition].ph->pageNum == pageNum) {
        // update a pool
        // printf("update\n");
        if (bm->strategy == RS_LRU) {
            pages[foundpoolposition].priority = ++globalpriority;
        }
        // pages[foundpoolposition].readcount++;
        pages[foundpoolposition].fixcount++;
        // readBlock(pageNum, &fh, pages[foundpoolposition].ph->data);
        // globalread++;
        page->pageNum = pageNum;
        strcpy(page->data, pages[foundpoolposition].ph->data);
    } else {
        // add a new or replace a pool

        if (pages[foundpoolposition].isactive == 1) {
            // check old page
            free(pages[foundpoolposition].ph->data);
            free(pages[foundpoolposition].ph);
        }

        BM_PageHandle *newpage = MAKE_PAGE_HANDLE();
        page->data = (SM_PageHandle)malloc(PAGE_SIZE);
        newpage->data = (SM_PageHandle)malloc(PAGE_SIZE);
        page->pageNum = pageNum;
        newpage->pageNum = pageNum;
        readBlock(pageNum, &fh, page->data);
        globalread++;
        strcpy(newpage->data, page->data);

        pages[foundpoolposition].isactive = 1;
        pages[foundpoolposition].readcount = 1;
        pages[foundpoolposition].writecount = 0;
        pages[foundpoolposition].fixcount = 1;
        pages[foundpoolposition].priority = ++globalpriority;
        pages[foundpoolposition].isdirty = false;
        pages[foundpoolposition].ph = newpage;

        // printf("addnew\n");
    }
    closePageFile(&fh);
    // printPoolContent(bm);
    // printpri(bm);
    // printf("\n");
    return RC_OK;
}

// Statistics Interface
PageNumber *getFrameContents(BM_BufferPool *const bm) {
    int *contents = malloc(sizeof(int) * bm->numPages);
    struct PageInfo *pages;
    pages = bm->mgmtData;

    for (int i = 0; i < bm->numPages; i++) {
        if (pages[i].isactive == 1) {
            contents[i] = pages[i].ph->pageNum;
        } else {
            contents[i] = -1;
        }
    }

    return contents;
}

bool *getDirtyFlags(BM_BufferPool *const bm) {
    bool *flags = (bool*)malloc(sizeof(bool) * bm->numPages);
    struct PageInfo *pages;
    pages = bm->mgmtData;

    for (int i = 0; i < bm->numPages; i++) {
        flags[i] = pages[i].isdirty;
    }

    return flags;
}

int *getFixCounts(BM_BufferPool *const bm) {
    int *fixcounts = malloc(sizeof(bool) * bm->numPages);
    struct PageInfo *pages;
    pages = bm->mgmtData;

    for (int i = 0; i < bm->numPages; i++) {
        fixcounts[i] = pages[i].fixcount;
    }

    return fixcounts;
}

int getNumReadIO(BM_BufferPool *const bm) {
    // calculate total read io
    int readio = 0;
    struct PageInfo *pages;
    pages = bm->mgmtData;

    for (int i = 0; i < bm->numPages; i++) {
        if (pages[i].isactive == 1) {
            readio += pages[i].readcount;
        }
    }

    // return readio;
    return globalread;
}

int getNumWriteIO(BM_BufferPool *const bm) {
    // calculate total write io
    int writeio = 0;
    struct PageInfo *pages;
    pages = bm->mgmtData;

    for (int i = 0; i < bm->numPages; i++) {
        if (pages[i].isactive == 1) {
            writeio += pages[i].writecount;
        }
    }

    // return writeio;
    return globalwrite;
}
