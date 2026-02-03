The Short Answer                                                                                                                                                 
                                                                                                                                                                   
  The RDB is VIRTUAL - created in RAM by fs-uae, NOT stored in the HDF file itself. When your bare-metal ROM reads block 0 via IDE, you get a synthesized RDB from 
  memory.                                                                                                                                                          
                                                                                                                                                                   
  How It Works                                                                                                                                                     
                                                                                                                                                                   
  1. Virtual RDB Structure                                                                                                                                         
                                                                                                                                                                   
  From hardfile.cpp:402-566, fs-uae creates a virtual RDB in memory:                                                                                               
                                                                                                                                                                   
  uae_u8 *rdb = xcalloc(uae_u8, size);  // Allocate RAM buffer                                                                                                     
  hfd->virtual_rdb = rdb;               // Store pointer                                                                                                           
  hfd->virtual_size = size;             // Typically ~262KB                                                                                                        
                                                                                                                                                                   
  This buffer contains:                                                                                                                                            
  - Block 0: RDB header ("RDSK" magic)                                                                                                                             
  - Block 1: PART block (partition table)                                                                                                                          
  - Block 2: FSHD block (filesystem header)                                                                                                                        
  - Block 3+: LSEG blocks (filesystem code)                                                                                                                        
                                                                                                                                                                   
  2. What Your ROM Sees When Reading Sectors                                                                                                                       
                                                                                                                                                                   
  When you issue READ SECTORS via IDE:                                                                                                                             
                                                                                                                                                                   
  Your ROM reads LBA 0:                                                                                                                                            
    ↓                                                                                                                                                              
  IDE emulation: offset = 0 * 512 = 0                                                                                                                              
    ↓                                                                                                                                                              
  hdf_read2() checks: offset < virtual_size?                                                                                                                       
    ↓ YES                                                                                                                                                          
  Returns: memcpy(buffer, virtual_rdb + 0, 512)                                                                                                                    
    ↓                                                                                                                                                              
  You get: "RDSK" + geometry + partition pointers                                                                                                                  
                                                                                                                                                                   
  Example reads:                                                                                                                                                   
  ; Read block 0                                                                                                                                                   
  LBA = 0 → Returns virtual_rdb[0..511]     (RDB header)                                                                                                           
                                                                                                                                                                   
  ; Read block 1                                                                                                                                                   
  LBA = 1 → Returns virtual_rdb[512..1023]  (PART block)                                                                                                           
                                                                                                                                                                   
  ; Read block 2                                                                                                                                                   
  LBA = 2 → Returns virtual_rdb[1024..1535] (FSHD block)                                                                                                           
                                                                                                                                                                   
  ; Read block 512 (after virtual RDB ends)                                                                                                                        
  LBA = 512 → offset = 512*512 = 262144                                                                                                                            
           → offset >= virtual_size                                                                                                                                
           → Reads from actual HDF file at offset (262144 - virtual_size)                                                                                          
                                                                                                                                                                   
  3. Block 0 Structure (What You'll Read)                                                                                                                          
                                                                                                                                                                   
  When you read block 0 via IDE at 0xDA0002, you'll get:                                                                                                           
                                                                                                                                                                   
  Offset  Size  Field           Value                                                                                                                              
  ------  ----  -----           -----                                                                                                                              
  0       4     ID              "RDSK" (0x5244534B)                                                                                                                
  4       4     SummedLongs     Size of checksummed area                                                                                                           
  8       4     ChkSum          Checksum                                                                                                                           
  12      4     HostID          SCSI ID (7)                                                                                                                        
  16      4     BlockBytes      512                                                                                                                                
  20      4     Flags           0x17                                                                                                                               
  24      4     BadBlockList    -1 (none)                                                                                                                          
  28      4     PartitionList   Block number of first PART (usually 1)                                                                                             
  32      4     FileSysHdrList  Block number of first FSHD (usually 2)                                                                                             
  36      4     DriveInit       0                                                                                                                                  
  ...                                                                                                                                                              
  64      4     Cylinders       From geometry                                                                                                                      
  68      4     Sectors         From geometry                                                                                                                      
  72      4     Heads           From geometry                                                                                                                      
  76      4     Interleave      1                                                                                                                                  
  80      4     Park            Cylinders                                                                                                                          
  84      4     Reserved                                                                                                                                           
  ...                                                                                                                                                              
                                                                                                                                                                   
  4. The Memory Layout                                                                                                                                             
                                                                                                                                                                   
  ┌─────────────────────────────────┐                                                                                                                              
  │   RAM (virtual_rdb buffer)      │                                                                                                                              
  │                                 │                                                                                                                              
  │ Block 0: RDB Header (512 bytes)│ ← LBA 0 reads here                                                                                                            
  │ Block 1: PART block (512 bytes)│ ← LBA 1 reads here                                                                                                            
  │ Block 2: FSHD block (512 bytes)│ ← LBA 2 reads here                                                                                                            
  │ Block 3+: LSEG blocks          │                                                                                                                               
  │ ...                             │                                                                                                                              
  │ (total ~262KB)                  │                                                                                                                              
  └─────────────────────────────────┘                                                                                                                              
                  ↓ virtual_size boundary                                                                                                                          
  ┌─────────────────────────────────┐                                                                                                                              
  │   HDF File (disk image)         │                                                                                                                              
  │                                 │                                                                                                                              
  │ Actual filesystem data          │ ← LBA 512+ reads here                                                                                                        
  │ Files, directories, etc.        │                                                                                                                              
  │                                 │                                                                                                                              
  └─────────────────────────────────┘                                                                                                                              
                                                                                                                                                                   
  5. Read/Write Handling                                                                                                                                           
                                                                                                                                                                   
  From hardfile.cpp:1122-1137:                                                                                                                                     
                                                                                                                                                                   
  // READ                                                                                                                                                          
  if (offset < hfd->virtual_size) {                                                                                                                                
      // Reading RDB area - return from RAM                                                                                                                        
      memcpy(buffer, hfd->virtual_rdb + offset, len);                                                                                                              
  } else {                                                                                                                                                         
      // Reading data area - read from file                                                                                                                        
      fseek(hfd->handle, offset - hfd->virtual_size);                                                                                                              
      fread(buffer, len, hfd->handle);                                                                                                                             
  }                                                                                                                                                                
                                                                                                                                                                   
  // WRITE                                                                                                                                                         
  if (offset < hfd->virtual_size) {                                                                                                                                
      // Writing to RDB area - IGNORED!                                                                                                                            
      // Virtual RDB is read-only                                                                                                                                  
  } else {                                                                                                                                                         
      // Writing to data area - write to file                                                                                                                      
      fseek(hfd->handle, offset - hfd->virtual_size);                                                                                                              
      fwrite(buffer, len, hfd->handle);                                                                                                                            
  }                                                                                                                                                                
                                                                                                                                                                   
  Critical: Writes to the RDB area are silently ignored!                                                                                                           
                                                                                                                                                                   
  6. When is RDB Created?                                                                                                                                          
                                                                                                                                                                   
  From hardfile.cpp:600-603:                                                                                                                                       
                                                                                                                                                                   
  fs-uae creates virtual RDB when:                                                                                                                                 
  - HDF file exists but block 0 is NOT "RDSK"                                                                                                                      
  - HDF file is a plain filesystem image (no partition table)                                                                                                      
                                                                                                                                                                   
  fs-uae uses physical RDB when:                                                                                                                                   
  - Block 0 starts with "RDSK" magic                                                                                                                               
  - HDF was created with partitions using HDToolBox                                                                                                                
                                                                                                                                                                   
  7. RDB vs Non-RDB Hardfiles                                                                                                                                      
                                                                                                                                                                   
  With Virtual RDB (FILESYS_HARDFILE_RDB):                                                                                                                         
  Block 0: "RDSK" + geometry + partition list                                                                                                                      
  Block 1: "PART" + partition info (DH0:)                                                                                                                          
  Block 2: "FSHD" + filesystem header                                                                                                                              
  Block 512+: Your actual filesystem data                                                                                                                          
                                                                                                                                                                   
  Without RDB (FILESYS_HARDFILE):                                                                                                                                  
  Block 0: Filesystem root block or zeros                                                                                                                          
  Block 1+: Your actual filesystem data immediately                                                                                                                
                                                                                                                                                                   
  8. What Your Bare-Metal ROM Should Do                                                                                                                            
                                                                                                                                                                   
  Step 1: Read Block 0                                                                                                                                             
  ; Read first sector into buffer                                                                                                                                  
      moveq   #0,d0           ; LBA 0                                                                                                                              
      lea     rdb_buffer,a0                                                                                                                                        
      bsr     read_sector                                                                                                                                          
                                                                                                                                                                   
      ; Check for RDB magic                                                                                                                                        
      cmpi.l  #'RDSK',(a0)                                                                                                                                         
      bne.s   no_rdb                                                                                                                                               
                                                                                                                                                                   
  Step 2: Parse RDB                                                                                                                                                
      ; Get geometry                                                                                                                                               
      move.l  64(a0),d0       ; Cylinders                                                                                                                          
      move.l  68(a0),d1       ; Sectors per track                                                                                                                  
      move.l  72(a0),d2       ; Heads                                                                                                                              
                                                                                                                                                                   
      ; Get partition list pointer                                                                                                                                 
      move.l  28(a0),d3       ; PartitionList block number                                                                                                         
                                                                                                                                                                   
      ; Read partition block                                                                                                                                       
      move.l  d3,d0                                                                                                                                                
      lea     part_buffer,a0                                                                                                                                       
      bsr     read_sector                                                                                                                                          
                                                                                                                                                                   
  Step 3: Find Bootable Partition                                                                                                                                  
      ; Check for PART magic                                                                                                                                       
      cmpi.l  #'PART',(a0)                                                                                                                                         
      bne.s   no_partition                                                                                                                                         
                                                                                                                                                                   
      ; Get boot priority                                                                                                                                          
      move.l  4(a0),d0        ; Next partition (-1 = none)                                                                                                         
      move.l  16(a0),d1       ; Flags (bootable?)                                                                                                                  
      move.l  20(a0),d2       ; Reserved                                                                                                                           
                                                                                                                                                                   
  9. Block Numbers in RDB                                                                                                                                          
                                                                                                                                                                   
  The RDB uses block numbers, not LBA:                                                                                                                             
                                                                                                                                                                   
  PartitionList = 1    means read LBA 1 for PART block                                                                                                             
  FileSysHdrList = 2   means read LBA 2 for FSHD block                                                                                                             
  NextPartition = -1   means no more partitions                                                                                                                    
                                                                                                                                                                   
  10. Practical Example                                                                                                                                            
                                                                                                                                                                   
  What happens with a 100MB HDF:                                                                                                                                   
                                                                                                                                                                   
  File size: 104,857,600 bytes                                                                                                                                     
  Virtual RDB: 262,144 bytes (512 blocks)                                                                                                                          
  Data start: Block 512                                                                                                                                            
                                                                                                                                                                   
  LBA 0-511:   Virtual RDB (from RAM)                                                                                                                              
  LBA 512+:    File data (from HDF file offset 0)                                                                                                                  
                                                                                                                                                                   
  When you read LBA 1000:                                                                                                                                          
    offset = 1000 * 512 = 512,000 bytes                                                                                                                            
    512,000 > 262,144 (virtual_size)                                                                                                                               
    Read from HDF at: 512,000 - 262,144 = 249,856                                                                                                                  
                                                                                                                                                                   
  Summary for Your ROM                                                                                                                                             
                                                                                                                                                                   
  1. RDB is VIRTUAL - exists only in fs-uae's RAM, not in the HDF file                                                                                             
  2. Block 0 returns "RDSK" - synthesized RDB header with geometry                                                                                                 
  3. Blocks 1-2 return partition/filesystem info - also virtual                                                                                                    
  4. Block 512+ returns actual data - from the HDF file                                                                                                            
  5. Writes to RDB are ignored - virtual RDB is read-only                                                                                                          
  6. Your ROM should:                                                                                                                                              
    - Read block 0 and check for "RDSK"                                                                                                                            
    - Parse geometry (cylinders, heads, sectors)                                                                                                                   
    - Read partition list to find bootable partition                                                                                                               
    - Load boot code from the partition's boot blocks                                                                                                              
                                                                                                                                                                   
  The virtual RDB makes fs-uae compatible with both partitioned and non-partitioned HDF files!                                                                     
