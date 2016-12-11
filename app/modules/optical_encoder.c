#include "module.h"
#include "lauxlib.h"
#include "platform.h"
#include "c_types.h"
#include "c_string.h"
#include "gpio.h"
#include "ets_sys.h"

// Keeps the count of interrupts detected, triggered by a positive edge
static int32_t counter = 0;

// Multiple identification of pin bound to interrupt detection
static unsigned node_pin;
static uint32_t pin_bit;


static uint32_t ICACHE_RAM_ATTR encoder_interrupt(uint32_t ret_gpio_status) 
{
  // This function really is running at interrupt level with everything
  // else masked off. It should take as little time as necessary.
  
  uint32 level = 0x1 & GPIO_INPUT_GET(GPIO_ID_PIN(pin_num[node_pin]));
  
  // Level can be 1 or 0, we only want to count POSITIVE edges, so..
  counter += level;

  // Cleaning interrupt
  GPIO_REG_WRITE(GPIO_STATUS_W1TC_ADDRESS, pin_bit);
  
  // Prevent further cleaning and callback calling in 
  // platform_gpio_intr_dispatcher() because it disables the interrupt 
  // and does not reenables it if there is not callback associated.
  ret_gpio_status &= ~pin_bit;

  return ret_gpio_status;
}

// Lua: setup( pin )
static int optical_encoder_setup(lua_State* L)
{  
  unsigned pin = luaL_checkinteger(L, 1);
  luaL_argcheck(L, platform_gpio_exists(pin) && pin > 0, 1, "Invalid interrupt pin");
  
  node_pin = pin;
  pin_bit = BIT(pin_num[node_pin]);
  
  // Pin configuration as INTERRUPT on POSITIVE edge, with the callback
  // encoder_interrupt() hooked to GPIO ISR.
  platform_gpio_mode(node_pin, PLATFORM_GPIO_INT, PLATFORM_GPIO_FLOAT);
  platform_gpio_intr_init(node_pin, GPIO_PIN_INTR_POSEDGE);
  platform_gpio_register_intr_hook(pin_bit, encoder_interrupt);
  return 0;
}

// Lua: get_counter()
static int optical_encoder_get_counter(lua_State* L)
{
  lua_pushinteger(L, counter);
  return 1;
}

// Lua: reset_counter()
static int optical_encoder_reset_counter(lua_State* L)
{
  counter = 0;
  return 0;
}

// Lua: close()
static int optical_encoder_close(lua_State* L)
{
  platform_gpio_intr_init(node_pin, GPIO_PIN_INTR_DISABLE);
  platform_gpio_mode(node_pin, PLATFORM_GPIO_INPUT, PLATFORM_GPIO_PULLUP);
  return 0;
}

// Module function map
static const LUA_REG_TYPE optical_encoder_map[] = {
  { LSTRKEY( "setup" ),         LFUNCVAL( optical_encoder_setup ) },
  { LSTRKEY( "get_counter" ),   LFUNCVAL( optical_encoder_get_counter ) },
  { LSTRKEY( "reset_counter" ), LFUNCVAL( optical_encoder_reset_counter ) },
  { LSTRKEY( "close" ),         LFUNCVAL( optical_encoder_close ) },

  { LNILKEY, LNILVAL }
};

NODEMCU_MODULE(OPTICAL_ENCODER, "opt_enc", optical_encoder_map, 0);