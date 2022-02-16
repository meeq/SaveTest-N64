/**
 * @file savetest.c
 * @author Christopher Bonhage (me@christopherbonhage.com)
 * @brief N64 test ROM for probing various cartridge save types
 */

#include <string.h>
#include <libdragon.h>

typedef struct PI_regs_s {
    volatile void * ram_address;
    uint32_t pi_address;
    uint32_t read_length;
    uint32_t write_length;
} PI_regs_t;
static volatile PI_regs_t * const PI_regs = (PI_regs_t *)0xA4600000;

#define CART_DOM2_ADDR2_START     0x08000000
#define CART_DOM2_ADDR2_SIZE      0x08000000

#define SRAM_256KBIT_SIZE         0x00008000
#define SRAM_768KBIT_SIZE         0x00018000
#define SRAM_1MBIT_SIZE           0x00020000
#define SRAM_BANK_SIZE            SRAM_256KBIT_SIZE
#define SRAM_256KBIT_BANKS        1
#define SRAM_768KBIT_BANKS        3
#define SRAM_1MBIT_BANKS          4

#define FLASHRAM_IDENTIFIER                0x11118001
#define FLASHRAM_OFFSET_COMMAND            0x00010000
#define FLASHRAM_COMMAND_SET_IDENTIFY_MODE 0xE1000000
#define FLASHRAM_COMMAND_SET_READ_MODE     0xF0000000

typedef uint8_t save_type_t;

#define SAVE_TYPE_NONE                    0x00
#define SAVE_TYPE_EEPROM_4KBIT            0x01
#define SAVE_TYPE_EEPROM_16KBIT           0x02
#define SAVE_TYPE_FLASHRAM_1MBIT          0x04
#define SAVE_TYPE_SRAM_256KBIT            0x08
#define SAVE_TYPE_SRAM_768KBIT_BANKED     0x10
#define SAVE_TYPE_SRAM_1MBIT_BANKED       0x20
#define SAVE_TYPE_SRAM_768KBIT_CONTIGUOUS 0x40
#define SAVE_TYPE_SRAM_1MBIT_CONTIGUOUS   0x80

typedef enum flashram_type_t
{
    FLASHRAM_TYPE_NONE        = 0x00000000,
    FLASHRAM_TYPE_MX29L0000   = 0x00C20000,
    FLASHRAM_TYPE_MX29L0001   = 0x00C20001,
    FLASHRAM_TYPE_MX29L1100   = 0x00C2001E,
    FLASHRAM_TYPE_MX29L1101_A = 0x00C2001D,
    FLASHRAM_TYPE_MX29L1101_B = 0x00C20084,
    FLASHRAM_TYPE_MX29L1101_C = 0x00C2008E,
    FLASHRAM_TYPE_MN63F81MPN  = 0x003200F1,
} flashram_type_t;

const char * format_flashram_detected(flashram_type_t flashram)
{
    switch ( flashram )
    {
        case FLASHRAM_TYPE_NONE:
            return "NO";
        case FLASHRAM_TYPE_MX29L0000:
            return "YES: Macronix MX29L0000";
        case FLASHRAM_TYPE_MX29L0001:
            return "YES: Macronix MX29L0001";
        case FLASHRAM_TYPE_MX29L1100:
            return "YES: Macronix MX29L1100";
        case FLASHRAM_TYPE_MX29L1101_A:
        case FLASHRAM_TYPE_MX29L1101_B:
        case FLASHRAM_TYPE_MX29L1101_C:
            return "YES: Macronix MX29L1101";
        case FLASHRAM_TYPE_MN63F81MPN:
            return "YES: Matsushita MN63F81MPN";
        default:
            return "YES: Unidentified chip";
    }
}

void cart_dom2_addr2_read(void * dest, uint32_t offset, uint32_t len)
{
    assert(dest != NULL);
    assert(offset < CART_DOM2_ADDR2_SIZE);
    assert(len > 0);
    assert(offset + len <= CART_DOM2_ADDR2_SIZE);

    disable_interrupts();
    dma_wait();

    MEMORY_BARRIER();
    PI_regs->ram_address = UncachedAddr(dest);
    MEMORY_BARRIER();
    PI_regs->pi_address = offset | CART_DOM2_ADDR2_START;
    MEMORY_BARRIER();
    PI_regs->write_length = len - 1;
    MEMORY_BARRIER();

    enable_interrupts();
    dma_wait();
}

void cart_dom2_addr2_write(const void * src, uint32_t offset, uint32_t len)
{
    assert(src != NULL);
    assert(offset < CART_DOM2_ADDR2_SIZE);
    assert(len > 0);
    assert(offset + len <= CART_DOM2_ADDR2_SIZE);

    disable_interrupts();
    dma_wait();

    MEMORY_BARRIER();
    PI_regs->ram_address = UncachedAddr(src);
    MEMORY_BARRIER();
    PI_regs->pi_address = offset | CART_DOM2_ADDR2_START;
    MEMORY_BARRIER();
    PI_regs->read_length = len - 1;
    MEMORY_BARRIER();

    enable_interrupts();
    dma_wait();
}

bool cart_detect_flashram(flashram_type_t * chip_id)
{
    io_write(
        CART_DOM2_ADDR2_START | FLASHRAM_OFFSET_COMMAND,
        FLASHRAM_COMMAND_SET_IDENTIFY_MODE
    );

    uint32_t __attribute__ ((aligned( 16 ))) silicon_id[2];
    data_cache_hit_writeback_invalidate( silicon_id, sizeof( silicon_id ) );
    cart_dom2_addr2_read( silicon_id, 0, sizeof( silicon_id ) );

    if( silicon_id[0] == FLASHRAM_IDENTIFIER )
    {
        if (chip_id != NULL) *chip_id = silicon_id[1];
        return true;
    }
    else
    {
        if (chip_id != NULL) *chip_id = FLASHRAM_TYPE_NONE;
        return false;
    }
}

void cart_sram_bank_read(void * dest, size_t bank, uint32_t offset, uint32_t len)
{
    assert(dest != NULL);
    assert(bank < SRAM_1MBIT_BANKS);
    assert(offset < SRAM_256KBIT_SIZE);
    assert(len > 0);
    assert(offset + len <= SRAM_256KBIT_SIZE);

    cart_dom2_addr2_read( dest, ((uint32_t)bank << 18) | offset, len );
}

void cart_sram_bank_write(const void * src, size_t bank, uint32_t offset, uint32_t len)
{
    assert(src != NULL);
    assert(bank < SRAM_1MBIT_BANKS);
    assert(offset < SRAM_256KBIT_SIZE);
    assert(len > 0);
    assert(offset + len <= SRAM_256KBIT_SIZE);

    cart_dom2_addr2_write( src, ((uint32_t)bank << 18) | offset, len );
}

bool cart_sram_bank_verify(size_t bank)
{
    assert(bank < SRAM_1MBIT_BANKS);

    uint8_t __attribute__ ((aligned(16))) write_buf[SRAM_BANK_SIZE];
    uint8_t __attribute__ ((aligned(16))) read_buf[SRAM_BANK_SIZE];

    /* Clear all previous SRAM banks to detect address wrapping */
    memset( write_buf, 0, SRAM_BANK_SIZE );
    for( size_t i = 0; i < bank; i++ )
    {
        /* No previous banks to erase if verifying the first one */
        if( bank == 0 ) break;
        /* Write all zeroes to the bank to read back later */
        data_cache_hit_writeback_invalidate( write_buf, SRAM_BANK_SIZE );
        cart_sram_bank_write( write_buf, i, 0, SRAM_BANK_SIZE );
    }

    /* Generate test values based on the destination SRAM addresses */
    uint32_t * write_words = (uint32_t *)write_buf;
    for( size_t i = 0; i < SRAM_BANK_SIZE / sizeof(uint32_t); ++i )
    {
        write_words[i] = ((uint32_t)bank << 18) + i;
    }

    /* Write the test values into SRAM */
    data_cache_hit_writeback_invalidate( write_buf, SRAM_BANK_SIZE );
    cart_sram_bank_write( write_buf, bank, 0, SRAM_BANK_SIZE );

    /* Read the test values back to see if they persisted */
    data_cache_hit_writeback_invalidate( read_buf, SRAM_BANK_SIZE );
    cart_sram_bank_read( read_buf, bank, 0, SRAM_BANK_SIZE );

    /* Compare what was written to what was read back from SRAM */
    if( memcmp( write_buf, read_buf, SRAM_BANK_SIZE ) != 0 )
    {
        /* There was a mismatch between what was written and read */
        return false;
    }

    /* Check that no previous banks were modified by changing this one */
    memset( write_buf, 0, SRAM_BANK_SIZE );
    for( size_t i = 0; i < bank; i++ )
    {
        /* No previous banks to check if verifying the first one */
        if( bank == 0 ) break;
        /* Read back the bank to see if it's still all zeroes */
        data_cache_hit_writeback_invalidate( read_buf, SRAM_BANK_SIZE );
        cart_sram_bank_read( read_buf, i, 0, SRAM_BANK_SIZE );
        if( memcmp( write_buf, read_buf, SRAM_BANK_SIZE ) != 0 )
        {
            /* The write test appears to have wrapped into another bank */
            return false;
        }
    }

    return true;
}

bool cart_sram_contiguous_verify(size_t capacity)
{
    assert(capacity <= SRAM_1MBIT_SIZE);

    uint8_t __attribute__ ((aligned(16))) write_buf[SRAM_1MBIT_SIZE];
    uint8_t __attribute__ ((aligned(16))) read_buf[SRAM_1MBIT_SIZE];

    /* Generate test values based on the destination SRAM addresses */
    uint32_t * write_words = (uint32_t *)write_buf;
    for( int i = 0; i < capacity / sizeof(uint32_t); ++i )
    {
        write_words[i] = i;
    }
  
    /* Write the test values into SRAM */
    data_cache_hit_writeback_invalidate( write_buf, capacity );
    cart_dom2_addr2_write( write_buf, 0, capacity );

    /* Read the test values back to see if they persisted */
    data_cache_hit_writeback_invalidate( read_buf, capacity );
    cart_dom2_addr2_read( read_buf, 0, capacity );

    /* Compare what was written to what was read back from SRAM */
    return memcmp( write_buf, read_buf, capacity ) == 0;
}

save_type_t cart_detect_save_type( flashram_type_t * flashram )
{
    save_type_t detected = SAVE_TYPE_NONE;

    eeprom_type_t eeprom = eeprom_present();
    if( eeprom == EEPROM_4K || eeprom == EEPROM_16K ) 
    {
        detected |= SAVE_TYPE_EEPROM_4KBIT;
        if( eeprom == EEPROM_16K ) 
        {
            detected |= SAVE_TYPE_EEPROM_16KBIT;
        }
    }

    if( cart_detect_flashram( flashram ) )
    {
        detected |= SAVE_TYPE_FLASHRAM_1MBIT;
    }
    else
    {
        if( cart_sram_contiguous_verify( SRAM_256KBIT_SIZE ) )
        {
            detected |= SAVE_TYPE_SRAM_256KBIT;
            /* Check the extended bank-selectable address spaces */
            if( cart_sram_bank_verify( 1 ) &&
                cart_sram_bank_verify( 2 ) )
            {
                detected |= SAVE_TYPE_SRAM_768KBIT_BANKED;
                if( cart_sram_bank_verify( 3 ) )
                {
                    detected |= SAVE_TYPE_SRAM_1MBIT_BANKED;
                }
            }
            /* Check the extended contiguous address spaces */
            if ( cart_sram_contiguous_verify( SRAM_768KBIT_SIZE ) )
            {
                detected |= SAVE_TYPE_SRAM_768KBIT_CONTIGUOUS;
                if ( cart_sram_contiguous_verify( SRAM_1MBIT_SIZE ) )
                {
                    detected |= SAVE_TYPE_SRAM_1MBIT_CONTIGUOUS;
                }
            }
        }
    }

    return detected;
}

int main(void)
{
    display_init(
        RESOLUTION_320x240,
        DEPTH_32_BPP,
        2,
        GAMMA_NONE,
        ANTIALIAS_RESAMPLE
    );
    console_init();
    debug_init_isviewer();

    printf( "LibDragon Save Type Detection Test ROM\n" );

    flashram_type_t flashram = FLASHRAM_TYPE_NONE;
    save_type_t detected = cart_detect_save_type( &flashram );
    printf( "\n" );

    printf( "Cartridge Save Type       | Detected\n" );
    printf( "--------------------------|----------------------------------\n" );
    printf( "EEPROM 4KBIT              |  %s\n", detected & SAVE_TYPE_EEPROM_4KBIT ? "YES" : "NO" );
    printf( "EEPROM 16KBIT             |  %s\n", detected & SAVE_TYPE_EEPROM_16KBIT ? "YES" : "NO" );
    printf( "FlashRAM 1MBIT            |  %s\n", format_flashram_detected( flashram )  );
    printf( "SRAM 256KBIT              |  %s\n", detected & SAVE_TYPE_SRAM_256KBIT ? "YES" : "NO" );
    printf( "SRAM 768KBIT (Banked)     |  %s\n", detected & SAVE_TYPE_SRAM_768KBIT_BANKED ? "YES" : "NO" );
    printf( "SRAM 1MBIT   (Banked)     |  %s\n", detected & SAVE_TYPE_SRAM_1MBIT_BANKED ? "YES" : "NO" );
    printf( "SRAM 768KBIT (Contiguous) |  %s\n", detected & SAVE_TYPE_SRAM_768KBIT_CONTIGUOUS ? "YES" : "NO" );
    printf( "SRAM 1MBIT   (Contiguous) |  %s\n", detected & SAVE_TYPE_SRAM_1MBIT_CONTIGUOUS ? "YES" : "NO" );

    if ( detected == SAVE_TYPE_NONE )
    {
        printf( "\n" );
        printf( "No cartridge save capabilities detected.\n" );
        printf( "Check your emulator/flashcart settings!\n" );
    }

    console_render();
}
