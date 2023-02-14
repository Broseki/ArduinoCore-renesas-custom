/* ########################################################################## */
/* - File: QSPIFlashBlockDevice.cpp
   - Copyright (c): 2023 Arduino srl.  All right reserved.
   - Author: Daniele Aimo (d.aimo@arduino.cc)

     This library is free software; you can redistribute it and/or
     modify it under the terms of the GNU Lesser General Public
     License as published by the Free Software Foundation; either
     version 2.1 of the License, or (at your option) any later version.

     This library is distributed in the hope that it will be useful,
     but WITHOUT ANY WARRANTY; without even the implied warranty of
     MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
     Lesser General Public License for more details.

     You should have received a copy of the GNU Lesser General Public
     License along with this library; if not, write to the Free Software
     Foundation, Inc.,51 Franklin St, Fifth Floor, Boston, MA  02110-1301 USA */
/* ########################################################################## */
#include "QSPIFlashBlockDevice.h"

#ifdef HAS_QSPI

/* -------------------------------------------------------------------------- */
/*                               CONSTRUCTOR                                  */
/* -------------------------------------------------------------------------- */
QSPIFlashBlockDevice::QSPIFlashBlockDevice(  pin_t _ck, 
                                             pin_t _cs, 
                                             pin_t _io0, 
                                             pin_t _io1, 
                                             pin_t _io2, 
                                             pin_t _io3) : 
   ck(_ck), cs(_cs), io0(_io0), io1(_io1), io2(_io2), io3(_io3),
   base_address((uint32_t)QSPI_DEVICE_START_ADDRESS),
   total_size((bd_size_t)QSPI_TOTAL_SIZE),
   read_block_size((bd_size_t)QSPI_READ_BLOCK_SIZE),
   erase_block_size((bd_size_t)QSPI_ERASE_BLOCK_SIZE),
   write_block_size((bd_size_t)QSPI_WRITE_BLOCK_SIZE) {
   
   ext_cfg.min_qssl_deselect_cycles = QSPI_QSSL_MIN_HIGH_LEVEL_4_QSPCLK;
   ext_cfg.qspclk_div = QSPI_QSPCLK_DIV_2;

   cfg.spi_protocol = SPI_FLASH_PROTOCOL_EXTENDED_SPI;
   cfg.read_mode = SPI_FLASH_READ_MODE_FAST_READ_QUAD_IO;
   cfg.address_bytes = SPI_FLASH_ADDRESS_BYTES_3;
   cfg.dummy_clocks = SPI_FLASH_DUMMY_CLOCKS_DEFAULT;

   cfg.page_program_address_lines = SPI_FLASH_DATA_LINES_1;
   cfg.page_size_bytes = 256;
   cfg.write_status_bit = 0;
   cfg.page_program_command = 0x02;
   cfg.write_enable_command = 0x06;
   cfg.status_command = 0x05;
   
   cfg.xip_enter_command = 0x20;
   cfg.xip_exit_command = 0xFF;
   cfg.erase_command_list_length = sizeof(g_qspi_erase_command_list) / sizeof(g_qspi_erase_command_list[0]);
   cfg.p_erase_command_list = &g_qspi_erase_command_list[0];
   cfg.p_extend = &ext_cfg;
}




/* -------------------------------------------------------------------------- */
/*                              DISTRUCTOR                                    */
/* -------------------------------------------------------------------------- */
QSPIFlashBlockDevice::~QSPIFlashBlockDevice() {

}

/* -------------------------------------------------------------------------- */
/*                              INIT - it calls open                          */
/* -------------------------------------------------------------------------- */
int QSPIFlashBlockDevice::init() {
   return open();
}

/* -------------------------------------------------------------------------- */
/*                            DEINIT - it calls close                         */
/* -------------------------------------------------------------------------- */
int QSPIFlashBlockDevice::deinit() {
   return close();
}

/* -------------------------------------------------------------------------- */
/*                         PROGRAM - 'remapped' on write                      */
/* -------------------------------------------------------------------------- */
int QSPIFlashBlockDevice::program(const void *buffer, bd_addr_t addr, bd_size_t _size) {
   return write(buffer, addr, _size);
}

/* -------------------------------------------------------------------------- */
/*                                  OPEN                                      */
/* -------------------------------------------------------------------------- */
int QSPIFlashBlockDevice::open() {
   static bool opened = false;
   fsp_err_t rv = (fsp_err_t)BLOCK_DEVICE_OK;

   if(!opened) {
      opened = true;

      R_IOPORT_PinCfg(NULL, g_pin_cfg[ck].pin,  (uint32_t) (IOPORT_CFG_PERIPHERAL_PIN | IOPORT_PERIPHERAL_QSPI));
      R_IOPORT_PinCfg(NULL, g_pin_cfg[cs].pin,  (uint32_t) (IOPORT_CFG_PERIPHERAL_PIN | IOPORT_PERIPHERAL_QSPI));
      R_IOPORT_PinCfg(NULL, g_pin_cfg[io0].pin, (uint32_t) (IOPORT_CFG_PERIPHERAL_PIN | IOPORT_PERIPHERAL_QSPI));
      R_IOPORT_PinCfg(NULL, g_pin_cfg[io1].pin, (uint32_t) (IOPORT_CFG_PERIPHERAL_PIN | IOPORT_PERIPHERAL_QSPI));
      R_IOPORT_PinCfg(NULL, g_pin_cfg[io2].pin, (uint32_t) (IOPORT_CFG_PERIPHERAL_PIN | IOPORT_PERIPHERAL_QSPI));
      R_IOPORT_PinCfg(NULL, g_pin_cfg[io3].pin, (uint32_t) (IOPORT_CFG_PERIPHERAL_PIN | IOPORT_PERIPHERAL_QSPI));
       
      uint8_t data_sreg[SREG_SIZE] = STATUS_REG_PAYLOAD;
      uint8_t sreg_data = 0;

      rv = R_QSPI_Open(&ctrl, &cfg);
      if(rv == FSP_SUCCESS) {
         rv = R_QSPI_DirectWrite(&ctrl, &(cfg.write_enable_command), ONE_BYTE, false);
      }
      if(rv == FSP_SUCCESS) {
         rv = get_flash_status();
      }
      if(rv == FSP_SUCCESS) {
         rv = R_QSPI_DirectWrite(&ctrl, data_sreg, SREG_SIZE, false);
      }
      if(rv == FSP_SUCCESS) {
         rv = get_flash_status();
      }
      if(rv == FSP_SUCCESS) {
         rv = R_QSPI_DirectWrite(&ctrl, &(cfg.status_command), ONE_BYTE, true);
      }
      if(rv == FSP_SUCCESS) {
         rv = R_QSPI_DirectRead(&ctrl, &sreg_data, ONE_BYTE);
      }        
      if(rv == FSP_SUCCESS) {
         if (SET_SREG_VALUE == sreg_data) {
            rv = (fsp_err_t)BLOCK_DEVICE_OK;
         }
      }
   }
   return (int)rv;
}

/* -------------------------------------------------------------------------- */
/*                                  GET FLASH STATUS                          */
/* -------------------------------------------------------------------------- */
fsp_err_t QSPIFlashBlockDevice::get_flash_status() {
   spi_flash_status_t status = {.write_in_progress = true};
   int32_t time_out          = (INT32_MAX);
   fsp_err_t err             = FSP_SUCCESS;

   do
   {
      /* Get status from QSPI flash device */
      err = R_QSPI_StatusGet(&ctrl, &status);
      if (FSP_SUCCESS != err)
      {
          return err;
      }

      /* Decrement time out to avoid infinite loop in case of consistent failure */
      --time_out;

      if (time_out <= 0)
      {
          return FSP_ERR_TIMEOUT;
      }

   } while (false != status.write_in_progress);

   return err;
}

/* -------------------------------------------------------------------------- */
/*                                  CLOSE                                     */
/* -------------------------------------------------------------------------- */
int QSPIFlashBlockDevice::close() {
   fsp_err_t rv = R_QSPI_Close(&ctrl);;
   return (int)rv;
}



#define READ_PAGE_SIZE   0x4000000
/* -------------------------------------------------------------------------- */
/* READ at the "logical" address 'add' 'size' bytes and copy them at the address
   specified by buffer
   NOTE: buffer MUST be equal or greater than 'size'                          */
/* -------------------------------------------------------------------------- */
int QSPIFlashBlockDevice::read(void *buffer, bd_addr_t add, bd_size_t _size) {
   fsp_err_t rv = FSP_ERR_INVALID_ADDRESS;

   if(buffer == nullptr) {
      rv = FSP_ERR_INVALID_ARGUMENT;
   }
   else {
      if(is_address_correct(add+_size-1)) {
         rv = (fsp_err_t)BLOCK_DEVICE_OK;
         int64_t internal_size = _size;
         uint32_t byte_left_in_bank = read_block_size - read_block_size % add;
         uint8_t *dest = (uint8_t *)buffer;
         while(internal_size > 0) {
            uint32_t bank = add / READ_PAGE_SIZE;
            #ifdef QSPI_FLASH_DEBUG
            Serial.print("+++++ READ Selected bank: ");
            Serial.println(bank);
            #endif
            uint32_t address = base_address + (add % READ_PAGE_SIZE);
            #ifdef QSPI_FLASH_DEBUG
            Serial.print("+++++ READ physical address: ");
            Serial.println(address,HEX);
            #endif
            R_QSPI_BankSet(&ctrl, bank);
            uint32_t bytes_to_copy = (internal_size > byte_left_in_bank) ? byte_left_in_bank : internal_size;
            memcpy(dest,(uint8_t *)address, bytes_to_copy);
            internal_size -= bytes_to_copy;
            add += bytes_to_copy;
            dest += bytes_to_copy;
            byte_left_in_bank = read_block_size;
         }
      }
   }

   return (int)rv; 
}

/* -------------------------------------------------------------------------- */
/* WRITE 'size' byte form buffer to the "logical" address specified by 'addr' 
   NOTE: buffer MUST be equal or greater than 'size'                          */
/* -------------------------------------------------------------------------- */
int QSPIFlashBlockDevice::write(const void *buffer, bd_addr_t add, bd_size_t _size) {
   fsp_err_t rv = FSP_ERR_INVALID_ADDRESS;
   if(buffer == nullptr) {
      rv = FSP_ERR_INVALID_ARGUMENT;
   }
   else {
      if(is_address_correct(add+_size-1)) {
         int64_t internal_size = _size;
         uint32_t bytes_left_in_page = write_block_size - (add % write_block_size);
         uint8_t *source = (uint8_t *)buffer;
         rv = FSP_SUCCESS;
         while(internal_size > 0 && rv == FSP_SUCCESS) {
            
            uint32_t bank = add / READ_PAGE_SIZE;
            #ifdef QSPI_FLASH_DEBUG
            Serial.print("+++++ WRITE Selected bank: ");
            Serial.println(bank);
            #endif
            
            uint32_t address = base_address + (add % READ_PAGE_SIZE);
            #ifdef QSPI_FLASH_DEBUG
            Serial.print("+++++ WRITE physical address: ");
            Serial.println(address,HEX);
            #endif
            
            R_QSPI_BankSet(&ctrl, bank);
            uint32_t bytes_to_write = (internal_size > bytes_left_in_page) ? bytes_left_in_page : internal_size;
            
            rv = R_QSPI_Write(&ctrl, source, (uint8_t*)address, bytes_to_write);
            internal_size -= bytes_to_write;
            source += bytes_to_write;
            add += bytes_to_write;
            bytes_left_in_page = write_block_size;
            if(rv == FSP_SUCCESS)
               rv = get_flash_status();
         }
      }
   }
   return (int)rv;
}

/* -------------------------------------------------------------------------- */
/* Tells if the "logical" address add is correct                              */
/* -------------------------------------------------------------------------- */
bool QSPIFlashBlockDevice::is_address_correct(bd_addr_t add) {
   return (add < total_size) ? true : false;
}

/* -------------------------------------------------------------------------- */
/*                                  ERASE                                     */
/* -------------------------------------------------------------------------- */
int QSPIFlashBlockDevice::erase(bd_addr_t add, bd_size_t _size) {
   
   fsp_err_t rv = FSP_ERR_INVALID_ADDRESS; 
   if(is_address_correct(add+_size-1)) {
      int64_t internal_size = _size;
      /* get the starting address of the block */
      uint32_t erase_block_address = erase_block_size * (add / erase_block_size);
      uint32_t byte_left_in_erase_block = erase_block_size - (add % erase_block_size);
      rv = FSP_SUCCESS;
      while(internal_size > 0 && rv == FSP_SUCCESS) {
         uint32_t bank = erase_block_address / READ_PAGE_SIZE;
         #ifdef QSPI_FLASH_DEBUG
         Serial.print("+++++ ERASE Selected bank: ");
         Serial.println(bank);
         #endif
         uint32_t address = base_address + (erase_block_address % READ_PAGE_SIZE);
         #ifdef QSPI_FLASH_DEBUG
         Serial.print("+++++ ERASE physical address: ");
         Serial.println(address,HEX);
         #endif
         R_QSPI_BankSet(&ctrl, bank);
         rv = R_QSPI_Erase(&ctrl, (uint8_t *)address, erase_block_size);
         erase_block_address += byte_left_in_erase_block;
         internal_size -= byte_left_in_erase_block;
         byte_left_in_erase_block = erase_block_size;
         if(rv == FSP_SUCCESS)
            rv = get_flash_status();
      }
   }
   
   return (int)rv;
}

/* -------------------------------------------------------------------------- */
/*                             GET BLOCK SIZEs                                */
/* -------------------------------------------------------------------------- */
bd_size_t QSPIFlashBlockDevice::get_program_size() const {
   return write_block_size;
}

bd_size_t QSPIFlashBlockDevice::get_erase_size() const {
   return erase_block_size;
}

bd_size_t QSPIFlashBlockDevice::get_read_size() const {
   return read_block_size;
}
/* -------------------------------------------------------------------------- */
/*                      GET TOTAL SIZE OF FLASH AVAILABLE                     */
/* -------------------------------------------------------------------------- */
bd_size_t QSPIFlashBlockDevice::size() const {
   return total_size;
}

const char *QSPIFlashBlockDevice::get_type() const {
    return "QSPIFLASH";
}

#endif