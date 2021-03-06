#include <sys/mman.h>
#include <stdlib.h>
#include <stddef.h>
#include <unistd.h>
#include <stdio.h>
#include <sys/sysinfo.h>
#include <assert.h>
#include <time.h>
#include <math.h>

//Compile with gcc -c buddy.c

//The smallest block i.e. 2^5(MIN) = 32 bytes
#define MIN 5
//The maximum amount of levels i.e. 32, 64.. 4 KByte maximum
//0 32, 1 64, 2 128, 3 256, 4 512, 5 1024, 6 2048, 7 4096
#define LEVELS 8
//Defines the size of the largest block(2^12 bytes), used in mmap
#define PAGE 4096

//Flag values for knowing if the block has been taken or not.
enum flag
{
    Free,
    Taken
};

//Benchmarking variables
//mmap count
int mmap_count = 0;

//Holds the double linked free lists of each layer
struct head *flists[LEVELS] = {NULL};

//Each block consists of a head and a byte sequence.
//The byte sequence is handed out to the requester(balloc caller)
struct head
{
    //This flag is used in the Buddy algorithm to merge blocks
    enum flag status;
    //Determines the size of the block. Index 0 is 32 bytes
    short int level;
    struct head *next;
    struct head *prev;
};

//Creates 4k blocks for the process. Traps to kernel.
struct head *new ()
{
    //The second argument gives the size, which in this case is 4096
    struct head *new = (struct head *)mmap(NULL,
                                           PAGE,
                                           PROT_READ | PROT_WRITE,
                                           MAP_PRIVATE | MAP_ANONYMOUS,
                                           -1,
                                           0);
    //Error handling
    if (new == MAP_FAILED)
    {
        return NULL;
    }
    //The 12 last bits should be zero
    assert(((long int)new & 0xfff) == 0);
    new->status = Free;
    //Should be 8 right?? Since level(32) is 1
    new->level = LEVELS - 1;
    mmap_count++;
    return new;
}

//Asterisk belongs to the return type.
//Given a block we want to find the buddy of it.
struct head *buddy(struct head *block)
{
    int index = block->level;
    //Generates a bitmask for getting the buddy
    long int mask = 0x1 << (index + MIN);
    // ^ XOR's and toggles the bit that differentiates this block from its buddy
    return (struct head *)((long int)block ^ mask);
}

//Merges 2 buddies
//Takes the header of the one with the lowest address and throws away the other
// struct head* merge(struct head* block, struct head* sibling) {
//     struct head* primary;
//     //Determines which block has the lowest address
//     if(sibling < block) {
//         primary = sibling;
//     } else {
//         primary = block;
//     }
//     //Increase level(size) of the primary block by 1 because of the merging
//     primary->level = primary->level + 1;
//     return primary;
// }

//Merges 2 buddies
//Always returns the primary block
struct head *merge(struct head *block)
{
    int index = block->level;
    long int mask = 0xffffffffffffffff << (1 + index + MIN);
    return (struct head *)((long int)block & mask);
}

//This should happen if we dont have a block of the right size
struct head *split(struct head *block)
{
    //Gets the block down one level
    int index = block->level-1;
    //Mask to find the buddy of the block, uses index so get the level right
    int mask = 0x1 << (index + MIN);
    //Effectively splitting the block
    return (struct head*)((long int)block | mask);
}

//Hides the secret head when returning an allocated block to the user(malloc).
void *hide(struct head *block)
{
    return (void *)(block + 1);
}

//Performed when converting a memory reference to a head structure(free)
struct head *magic(void *memory)
{
    return ((struct head *)memory - 1);
}

//Used to determine which block to allocate.
int level(int req)
{
    int total = req + sizeof(struct head);
    int i = 0;
    int size = 1 << MIN;
    while (total > size)
    {
        size <<= 1;
        i += 1;
    }
    //THE -1 WAS ADDED
    return i;
}

//Finds a free block to balloc given an index(level).
//Needs to unlink the block from the linked list
//Needs to split blocks that are way to big
struct head *find(int index)
{
    struct head *new_block;
    //Check there is a free block at this index level
    //If no block exists, use new to allocate a new one and recursively
    //split the block down to the required level
    if (flists[index] == NULL)
    {
        //Create new block, level->7 and status->Free set in new()
        new_block = new();
        //Split the block until the required size has been reached
        for (int i = 6; i >= index; i--)
        {
            //printf("Iteration/Level %d: ", i);
            //printf("Block %p, ", new_block);
            new_block = split(new_block);
            new_block->status = Free;
            buddy(new_block)->status = Free;
            new_block->level = i;
            buddy(new_block)->level = i;
            //printf("Split %p, Buddy(split) %p, Buddy(Buddy(split)) %p\n", new_block, buddy(new_block), buddy(buddy(new_block)));
            //printf("Split level %d, merge(split) %p, merge(split) level %d\n", new_block->level, merge(new_block), merge(new_block)->level);
            if (flists[i] != NULL)
            {
                //The first block in the list
                struct head *first_block = flists[i];
                //Pointer operations
                first_block->prev = buddy(new_block);
                buddy(new_block)->next = first_block;
            }
            flists[i] = buddy(new_block);
            //new_block = split(new_block);
        }
        new_block->status = Taken;
        //printf("Returning block %p with level %d\n", new_block, new_block->level);
        return new_block;
        //new_block->level = index;
    }
    else
    {
        //Unlink the first item in the free list
        //Relink flists[index] to the next item in free list
        new_block = flists[index];
        //Link the array to the rest of the list
        if (new_block->next != NULL)
        {
            flists[index] = new_block->next;
            new_block->next->prev = NULL;
        }
        else
        {
            flists[index] = NULL;
        }
        //Unlink the block
        new_block->next = NULL;
        new_block->prev = NULL;
    }

    new_block->status = Taken;
    // printf("Should return block with index %d: %d\n", index, new_block->level);
    //If there is a free block, fix the list and return the block(with the header)
    return new_block;
}

//Inserts a block into one of the free lists
//Needs to check for merging recursively
void insert(struct head *block)
{
    // printf("FREE START:\n");
    
    int index = block->level;
    block->status = Free;
    struct head *bud;
    struct head *primary;
    
    //Merge recursively
    // printf("Inserting block %p at level %d, ", block, index);
    if(block->level != 7) {
        // printf("Buddy %p, buddy(buddy()) %p, buddy(buddy(buddy(()) %p\n", buddy(block), buddy(buddy(block)), buddy(buddy(buddy(block))));
    }
    for (int i = index; i <= 7; i++)
    {
        // printf("Iteration %d\n", i);
        if(i!=7) {
            bud = buddy(block); //Segmentation error for i=7 sometime
        }
        if (bud->status == Free && bud->level == block->level && i!=7)
        {
            //Unlink the buddy from the list, can be anywhere in the list
            if(bud->prev == NULL) { 
                // printf("Buddy %p, level %d is first in the list, unlinking\n", bud, bud->level);
                flists[bud->level] = bud->next;
                // printf("Working\n");
                if(bud->next != NULL) {
                    bud->next->prev = NULL;
                }
            } else {
                // printf("Buddy %p, level %d is later in the list, unlinking\n", bud, bud->level);
                bud->prev->next = bud->next;
                if(bud->next != NULL) {
                    bud->next->prev = bud->prev;
                }
            }
            bud->next = NULL;
            bud->prev = NULL;

            int pre_merge_index = block->level;
            // printf("Buddy %p of block %p at level %d is free!\n", bud, block, block->level);
            block = merge(block); //Does not increase level of block
            // printf("Post-merge block id %p level %d\n", block, block->level);
            block->level = pre_merge_index+1; //Increase block level after merge 
        }
        else
        {
            // printf("No more merges! Inserting block %p at index/iteration %d and level %d!\n", block, i, block->level);

            //If the list is empty
            if (flists[block->level] == NULL)
            {
                // printf("List is at level %d is empty, inserting\n", block->level);
                flists[block->level] = block;
                break;
            }
            else
            {
                // printf("List at level %d is NOT empty, inserting\n", block->level);
                //Get the first block
                struct head *first_block = flists[block->level];
                if(first_block == block) {
                    printf("ERROR: The block is already in the free list!\n");
                }

                //Change pointers
                first_block->prev = block;
                block->next = first_block;
                block->prev = NULL;
                flists[block->level] = block;
                if(block->next == block) {
                    printf("ERROR: block->next pointing to itself!\n");
                }

                break;
            }
        }
    }
    block->status = Free;
}

void *balloc(size_t size)
{
    //Dont allocate anything of size 0
    if (size == 0)
    {
        return NULL;
    }
    //Find the appropriate level for the size
    int index = level(size);
    //Go through the free list of sizes and get a free block
    struct head *taken = find(index);
    //Hide the header and return the block to the user
    return hide(taken);
}

void bfree(void *memory)
{
    //Error handling
    if (memory != NULL)
    {
        //Go back to the header
        struct head *block = magic(memory);
        //Insert the block into the list free list
        insert(block);
    }
    return;
}

//Should be called for every balloc
//Checks that all pointers are correct
void sanity()
{
    printf("\nSANITY CHECK:\n");
    for (int i = 0; i <= 7; i++)
    {
        if (flists[i] != NULL)
        {
            struct head *block = flists[i];
            printf("First block ID %p, level %d, Back %p, Next %p, Taken %d\n", block, block->level, block->prev, block->next, block->status);
            while (block->next != NULL)
            {
                block = block->next;
                printf("    List block ID %p, Level %d, Back %p, Next %p, Taken %d\n", block, block->level, block->prev, block->next, block->status);
            }
        }
    }
    printf("\n");
}

//Used for testing basic functions
void test()
{
    printf("Level for 5 should be 0: %d\n", level(5));
    printf("Level for 72 should be 2: %d\n", level(72));
    printf("Level for 2000 should be 6: %d\n", level(2000));
    printf("Level for 4000 should be 7: %d\n", level(4000));
    printf("Size of a head is: %ld\n", sizeof(struct head));
    // struct head *new_block = new ();
    // printf("The new block level should be 7: %d\n", new_block->level);
    // printf("Level of new block: %d\n", new_block->level);
    // struct head *split_block = split(new_block);
    // split_block->level = new_block->level - 1;
    // printf("Level of split block: %d\n", split_block->level);
    // struct head *merged_block = merge(split_block);
    // printf("Level of merged block: %d\n", merged_block->level);
    // void *memory = hide(merged_block);
    // struct head *revealed = magic(memory);
    // printf("Level of block revealed: %d\n", revealed->level);
    // if (flists[7] == NULL)
    // {
    //     printf("Pointer to the level 7 of the array: %s\n", flists[7]);
    // }

    // printf("\n\nBalloc Tests: \n");
    // void* tes = balloc(6);
    // void* tes2 = balloc(2727);
    // void* tes3 = balloc(3734);
    // sanity();
    // bfree(tes);
    // sanity();
    // bfree(tes2);
    // bfree(tes3);
    // sanity();

    for(int i = 0; i < 20; i++) {   
        int size_1 = (rand()%(4000-1))+1;
        int size_2 = (rand()%(4000-1))+1;
        int size_3 = (rand()%(4000-1))+1;
        printf("Testing Size %d, %d, %d\n", size_1, size_2, size_3);
        void* p1 = balloc(size_1);
        void* p2 = balloc(size_2);
        void* p3 = balloc(size_3);
        bfree(p1);
        bfree(p2);
        bfree(p3);
        sanity();
    }
    
    //behold = split(behold);
    //behold->level = 6;
    //buddy(behold)->level = 6; //THE BUDDY DOES NOT KNOW ITS LEVEL AND BUDDY NEEDS THE LEVEL
    //printf("level %d, block %p, buddy %p, budbud %p, budbudbud %p, split %p, buddy(split) %p, merge(split) %p\n", behold->level, behold, buddy(behold), buddy(buddy(behold)), buddy(buddy(buddy(behold))), split(behold), buddy(split(behold)), merge(split(behold)));
    struct head* behold = new(); //new() fixes the level to 7
    printf("adress of new block %p\n", behold);
    printf("end of new block %p\n", behold+PAGE);
    //printf("level %d, block %p, buddy %p, budbud %p, budbudbud %p, split %p, buddy(split) %p, merge(split) %p, merge(buddy(split)) %p\n", behold->level, behold, buddy(behold), buddy(buddy(behold)), buddy(buddy(buddy(behold))), split(behold), buddy(split(behold)), merge(split(behold)), merge(buddy(split(behold))));
    for(int i = 6; i > 2; i--) {
        behold = split(behold);
        behold->level = i;
        buddy(behold)->level = i; //BUDDY NEEDS THE LEVEL TO GO BACK
        split(behold)->level = i-1; //MERGE NEEDS THE LEVEL TO MERGE, not necessary in real code
        printf("level %d, block %p, buddy %p, budbud %p, budbudbud %p, split %p, buddy(split) %p, merge(split) %p, merge(buddy(split)) %p\n", behold->level, behold, buddy(behold), buddy(buddy(behold)), buddy(buddy(buddy(behold))), split(behold), buddy(split(behold)), merge(split(behold)), merge(buddy(split(behold))));       
    }

    // struct head* behold = new();
    // behold->level = 7;
    // behold = split(behold);
    // printf("New %d, Buddy %d, Split %d, buddy(split) %d, buddy(buddy(split)) %d, Merged(should be equal to original buddy) %d\n", behold, buddy(behold), split(behold), buddy(split(behold)), buddy(buddy(split(behold))), merge(split(behold)));
    // printf("Merge split from buddy %d, merge split %d\n", merge(buddy(split(behold))), merge(split(behold)));
    // printf("Behold %d, Hide %d, Hide/Reveal %d\n", behold, hide(behold), magic(hide(behold)));
    // printf("Block %d, Buddy %d, Buddy(Buddy) %d\n", behold, buddy(behold), buddy(buddy(behold)));
    // printf("YOU NEED TO SPLIT BEFORE YOU USE BUDDY; FUCK buddy() does not work for new blocks\n");
    // void *balloc_test = balloc(2000);
    // void* balloc_test3;
    // balloc_test = balloc(990);
    // balloc_test3 = balloc(5);
    // balloc_test = balloc(2000);
    // balloc_test = balloc(2000);
    // sanity();
    // bfree(balloc_test);
    // sanity();
    // // void* balloc_test2 = balloc(900);
    // // sanity();
    // // bfree(balloc_test2);
    // // sanity();
    // bfree(balloc_test3);
    // //sanity();
}

void bench() {
    printf("#Benchtime! \n");
    printf("#ms	 kbyte\n");
    //printf("0 8 3.5\n2 5 2.2\n10 4 1.6\n12 8 4.4\n14 16 25\n");

    // double start_time = clock(); 
    // test();
    // double stop_time = clock(); 
    // double time_taken = ((double)stop_time - start_time) * 1000 /CLOCKS_PER_SEC; // in seconds 
    // printf("#Time taken %f\n", time_taken);
    int level_to_size[8] = {32, 64, 128, 256, 512, 1024, 2048, 4096};
    int seed = time(NULL);
    srand(seed);
    int allocated_end = 0;
    int end = 100;
    int probability = 0;
    void* allocated[end];
    int balloced[end];
    int allocated_memory = 0;
    int allocated_blocks = 0;
    int balloc_prob = 70;

    for(int time = 0; time < end; time++) {
        probability = (rand()%(100-0))+0;
        printf("%d ", time);
        printf("%d ", mmap_count*4096);
        printf("%d ", allocated_memory);
        printf("%d\n", allocated_blocks);
        if(probability >= balloc_prob && allocated_end > 0) {  
            bfree(allocated[allocated_end]);
            allocated[allocated_end] = NULL;
            allocated_memory -= balloced[allocated_end];
            allocated_blocks -= level_to_size[level(balloced[allocated_end])];
            //printf("FREE: balloced %d, Level %d, Block size %d \n", balloced[allocated_end], level(balloced[allocated_end]), level_to_size[level(balloced[allocated_end])]);
            balloced[allocated_end] = 0;
            allocated_end--;   
            balloc_prob--;
        } else {
            allocated_end++;
            int temp = (rand()%(4000-1))+1;
            allocated[allocated_end] = balloc(temp);
            allocated_memory += temp;
            balloced[allocated_end] = temp;
            allocated_blocks += level_to_size[level(balloced[allocated_end])];
            //printf("ALLOC: balloced %d, Level %d, Block size %d \n", balloced[allocated_end], level(balloced[allocated_end]), level_to_size[level(balloced[allocated_end])]);
        }
    }
    // printf("%d", (int)pow(2, 2));
}