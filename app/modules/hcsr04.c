#include "module.h"
#include "lauxlib.h"
#include "platform.h"
#include "c_types.h"
#include "c_string.h"
#include "gpio.h"
#include "hw_timer.h"
#include "task/task.h"
#include "ets_sys.h"


#define PULSE_US_TO_MM(x) (((x) * 10) / 58)


// Variable used in ISR to compute the pulse duration.
static int32_t time_elapsed = 0;

// Last pulse duration of the last pulse received in ECHO, in microseconds.
static uint32_t last_pulse_time = 0;

// Identificator of the task that calls the callback when a
// new distance has been obtained.
static uint32_t task_number;

// Flag that indicates whether the task that calls the callback 
// has been queued or not.
static uint32_t task_queued = 0;

// Reference in Lua Registry to callback passed in setup()
static int callback_ref;

// HC-SR04 pins, pin number is the number printed in the NodeMCU board.
unsigned echo_pin;
unsigned trigger_pin;


static uint32_t ICACHE_RAM_ATTR echo_interrupt(uint32_t ret_gpio_status) 
{
  // This function really is running at interrupt level with everything
  // else masked off. It should take as little time as necessary.

  uint32 level = 0x1 & GPIO_INPUT_GET(GPIO_ID_PIN(pin_num[echo_pin]));
  uint32_t time_now = system_get_time();

  if (time_elapsed == 0 && level)
  {
    time_elapsed = -time_now;
  }
  else if (time_elapsed < 0 && !level)
  {
    last_pulse_time = time_now + time_elapsed;
    time_elapsed = 0;
	
    if (!task_queued) {
	    if (task_post_medium(task_number, 0)) {
	      task_queued = 1;
	    }
	  }
  }
  
  // Prevent further cleaning and callback calling in 
  // platform_gpio_intr_dispatcher() because it disables the interrupt 
  // and does not reenables it if there is not callback associated.
  GPIO_REG_WRITE(GPIO_STATUS_W1TC_ADDRESS, BIT(pin_num[echo_pin]));
  ret_gpio_status &= ~BIT(pin_num[echo_pin]);

  return ret_gpio_status;
}


// Calls the function passed as argument in setup, distance_obtained_callback,
// with an argument, the last distance computed, in mm.
static void callback_task(os_param_t param, uint8_t prio)
{
  lua_State *L = lua_getstate();
  lua_rawgeti(L, LUA_REGISTRYINDEX, callback_ref);
  lua_pushinteger(L, PULSE_US_TO_MM(last_pulse_time));
  lua_call(L, 1, 0);
  task_queued = 0;
}

// Lua: setup( trigger_pin, echo_pin, distance_obtained_callback )
static int hcsr04_setup(lua_State* L)
{
  unsigned t_pin = luaL_checkinteger(L, 1);
  unsigned e_pin = luaL_checkinteger(L, 2);
  
  luaL_argcheck(L, platform_gpio_exists(t_pin), 1, "Invalid trigger pin");
  luaL_argcheck(L, platform_gpio_exists(e_pin) && e_pin > 0, 2, "Invalid echo pin, cannot be used for interrupt");
  
  if (lua_type(L, 3) == LUA_TFUNCTION || lua_type(L, 3) == LUA_TLIGHTFUNCTION)
  {
	// Pushes argument to Lua stack
    lua_pushvalue(L, 3);
	
	// Pops element from Lua stack, and creates a reference for it in Registry table
    callback_ref = luaL_ref(L, LUA_REGISTRYINDEX); 
  } else {
    luaL_argcheck(L, 0, 3, "invalid callback type");
  }
  
  trigger_pin = t_pin;
  echo_pin = e_pin;
  
  task_number = task_get_id(callback_task);
  
  // Pin configuration as INTERRUPT on BOTH edges, with the callback
  // echo_interrupt() hooked to GPIO ISR.
  platform_gpio_mode(trigger_pin, PLATFORM_GPIO_OUTPUT, PLATFORM_GPIO_FLOAT);
  platform_gpio_mode(echo_pin, PLATFORM_GPIO_INT, PLATFORM_GPIO_FLOAT);
  platform_gpio_intr_init(echo_pin, GPIO_PIN_INTR_ANYEDGE);
  platform_gpio_register_intr_hook(BIT(pin_num[echo_pin]), echo_interrupt);
  return 0;
}

// Lua: trigger()
static int hcsr04_trigger(lua_State* L)
{
  time_elapsed = 0;
  
  // A 20 us pulse is set in trigger pin
  platform_gpio_write(trigger_pin, PLATFORM_GPIO_HIGH);
  os_delay_us(20);
  platform_gpio_write(trigger_pin, PLATFORM_GPIO_LOW);
  return 0;
}

// Lua: close()
static int hcsr04_close(lua_State* L)
{
  platform_gpio_intr_init(echo_pin, GPIO_PIN_INTR_DISABLE);
  platform_gpio_mode(echo_pin, PLATFORM_GPIO_INPUT, PLATFORM_GPIO_PULLUP);
  return 0;
}


// Module function map
static const LUA_REG_TYPE hcsr04_map[] = {
  { LSTRKEY( "setup" ),    LFUNCVAL( hcsr04_setup ) },
  { LSTRKEY( "trigger" ),  LFUNCVAL( hcsr04_trigger ) },
  { LSTRKEY( "close" ),  LFUNCVAL( hcsr04_close ) },

  { LNILKEY, LNILVAL }
};

NODEMCU_MODULE(HCSR04, "hcsr04", hcsr04_map, 0);