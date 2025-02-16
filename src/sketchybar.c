#include "mach.h"
#include "parsing.h"
#include <lauxlib.h>
#include <lua.h>
#include <lualib.h>
#include <CoreFoundation/CoreFoundation.h>
#include <stdint.h>

#include "stack.h"

#define CMD_SUCCESS 1
#define CMD_FAILURE 0

lua_State* g_state;

#define BAR       "--bar"
#define SET       "--set"
#define ADD       "--add"
#define ANIMATE   "--animate"
#define DEFAULT   "--default"
#define SUBSCRIBE "--subscribe"
#define UPDATE    "--update"
#define QUERY     "--query"
#define HOTLOAD   "--hotload"
#define TRIGGER   "--trigger"
#define PUSH      "--push"
#define REMOVE    "--remove"

#define MACH_HELPER_FMT "git.lua.sketchybar%d"

struct callback {
  int callback_ref;
  char* name;
  char* event;
};

struct callbacks {
  struct callback** callbacks;
  uint32_t num_callbacks;
};

struct callbacks g_callbacks;
static char* g_cmd = NULL;
static uint32_t g_cmd_len = 0;
static char g_bootstrap_name[64];
mach_port_t g_port = 0;
uint32_t g_uid_counter;
char g_bs_lookup[256] = "git.felix.sketchybar";

static char *luat_to_string(int type) {
  switch (type) {
    case LUA_TNIL:      return "nil";      break;
    case LUA_TBOOLEAN:  return "boolean";  break;
    case LUA_TNUMBER:   return "number";   break;
    case LUA_TSTRING:   return "string";   break;
    case LUA_TTABLE:    return "table";    break;
    case LUA_TFUNCTION: return "function"; break;
    default:            return "unmapped"; break;
  }
}

static char* sketchybar(struct stack* stack) {
  uint32_t message_length;
  char* message = stack_flatten_ttb(stack, &message_length);

  if (!message && !g_cmd) return NULL;
  if (g_cmd && stack) {
    g_cmd = realloc(g_cmd, g_cmd_len + message_length);
    memcpy(g_cmd + g_cmd_len, message, message_length);
    g_cmd_len = g_cmd_len + message_length;
    free(message);
    return NULL;
  } else if (g_cmd && !stack) {
    message = g_cmd;
    message_length = g_cmd_len;
  }

  if (!g_port) g_port = mach_get_bs_port(g_bs_lookup);
  char message_format[message_length + 1];
  memcpy(message_format, message, message_length);
  message_format[message_length] = '\0';
  char* response = mach_send_message(g_port,
                                     message_format,
                                     message_length + 1,
                                     true               );
  if (!response) {
    g_port = mach_get_bs_port(g_bs_lookup);
    response = mach_send_message(g_port,
                                 message_format,
                                 message_length + 1,
                                 true               );
  }
  return response;
}

static void sketchybar_call_log_and_cleanup(struct stack* stack) {
  char* response = sketchybar(stack);
  if (response) {
    if (strlen(response) > 0) printf("[i] sketchybar: %s\n", response);
    free(response);
  }
  stack_destroy(stack);
}

static int transaction_create(lua_State* state) {
  if (!g_cmd) {
    g_cmd = malloc(1);
    g_cmd_len = 0;
    *g_cmd = '\0';
  }
  return 0;
}

static int transaction_commit(lua_State* state) {
  char* response = NULL;
  if (g_cmd) {
    response = sketchybar(NULL);
    free(g_cmd);
    if (response) {
      if (strlen(response) > 0) printf("[i] sketchybar: %s\n", response);
      free(response);
    }
    g_cmd_len = 0;
    g_cmd = NULL;
  }
  return 0;
}

int animate(lua_State* state) {
  if (lua_gettop(state) < 3
      || lua_type(state, -1) != LUA_TFUNCTION
      || lua_type(state, -2) != LUA_TNUMBER
      || lua_type(state, -3) != LUA_TSTRING   ) {
    char error[] = "[Lua] Error: expecting a string "
                   "a number and a function as arguments for 'animate'";
    printf("%s\n", error);
    return 0;
  }
  const int duration = lua_tointeger(state, -2);
  char duration_str[4];
  snprintf(duration_str, 4, "%d", duration);
  const char* interp = lua_tostring(state, -3);

  transaction_create(state);

  struct stack* stack = stack_create();
  stack_init(stack);
  stack_push(stack, duration_str);
  stack_push(stack, interp);
  stack_push(stack, ANIMATE);

  sketchybar_call_log_and_cleanup(stack);

  int error = lua_pcall(state, 0, 0, 0);

  if (error && lua_gettop(state)) {
    printf("[!] Lua: %s\n", lua_tostring(state, -1));
  }
  transaction_commit(state);
  return 0;
}

const char* get_name_from_state(lua_State* state) {
  const char* name;
  if (lua_type(state, 1) == LUA_TTABLE) {
    lua_getfield(state, 1, "self");
    if (lua_isnil(state, -1)) lua_pop(state, 1);
    lua_getfield(state, 1, "name");
    name = lua_tostring(state, -1);
    lua_pop(state, 1);
  } else {
    name = lua_tostring(state, 1);
  }
  return name;
}

int set(lua_State* state) {
  if (lua_gettop(state) < 2
      || lua_type(state, -1) != LUA_TTABLE
      || (lua_type(state, 1) != LUA_TSTRING)
          && lua_type(state, 1) != LUA_TTABLE) {
    char error[] = "[Lua] Error: expecting a "
                   " table as arguments for 'set'";
    printf("%s\n", error);
    return 0;
  }
  struct stack* stack = stack_create();
  stack_init(stack);

  const char* name = get_name_from_state(state);

  parse_kv_table(state, NULL, stack);

  stack_push(stack, name);
  stack_push(stack, SET);
  sketchybar_call_log_and_cleanup(stack);
  return 0;
}

int defaults(lua_State* state) {
  if (lua_gettop(state) < 1 || lua_type(state, -1) != LUA_TTABLE) {
    char error[] = "[Lua] Error: expecting a table as an"
                   "argument to function 'defaults'";
    printf("%s\n", error);
    return 0;
  }

  struct stack* stack = stack_create();
  stack_init(stack);
  parse_kv_table(state, NULL, stack);
  stack_push(stack, DEFAULT);

  sketchybar_call_log_and_cleanup(stack);
  return 0;
}

int bar(lua_State* state) {
  if (lua_gettop(state) < 1 || lua_type(state, -1) != LUA_TTABLE) {
    char error[] = "[Lua] Error: expecting a table as an "
                   "argument to function 'bar'";
    printf("%s\n", error);
    return 0;
  }
  struct stack* stack = stack_create();
  stack_init(stack);
  parse_kv_table(state, NULL, stack);
  stack_push(stack, BAR);

  sketchybar_call_log_and_cleanup(stack);
  return 0;
}

void callback_function(char* message, size_t len) {
  if (len > 1 + sizeof(int) && message && *message == '\x07') {
    int callback_ref = 0;
    memcpy(&callback_ref, message + 1, sizeof(int));
    char* response = message + 1 + sizeof(int);
    lua_rawgeti(g_state, LUA_REGISTRYINDEX, callback_ref);
    if (!json_to_lua_table(g_state, response)) {
      lua_pushstring(g_state, response);
    }

    transaction_create(g_state);
    int error = lua_pcall(g_state, 1, 0, 0);

    if (error && lua_gettop(g_state)) {
      printf("[!] Lua: %s\n", lua_tostring(g_state, -1));
    }
    transaction_commit(g_state);
    return;
  }
  env env = message;
  char* name = env_get_value_for_key(env, "NAME");
  char* sender = env_get_value_for_key(env, "SENDER");

  for (int i = 0; i < g_callbacks.num_callbacks; i++) {
    if (strcmp(g_callbacks.callbacks[i]->name, name) == 0
        && strcmp(g_callbacks.callbacks[i]->event, sender) == 0) {

      lua_rawgeti(g_state, LUA_REGISTRYINDEX,
                           g_callbacks.callbacks[i]->callback_ref);

      lua_newtable(g_state);
      struct key_value_pair kv = { NULL, NULL };
      do {
        kv = env_get_next_key_value_pair(env, kv);
        if (kv.key && kv.value) {
          lua_pushstring(g_state, kv.key);
          if (!json_to_lua_table(g_state, kv.value)) {
            lua_pushstring(g_state, kv.value);
          }

          lua_settable(g_state,-3);
        }
      } while(kv.key && kv.value);

      transaction_create(g_state);
      int error = lua_pcall(g_state, 1, 0, 0);

      if (error && lua_gettop(g_state)) {
        printf("[!] Lua: %s\n", lua_tostring(g_state, -1));
      }
      transaction_commit(g_state);
      break;
    }
  }
}

void subscribe_register_event(const char* name, const char* event, int callback_ref) {
  struct stack* stack = stack_create();
  char mach_helper[strlen(g_bootstrap_name) + 16];
  snprintf(mach_helper, strlen(g_bootstrap_name) + 16, "mach_helper=%s",
                                                        g_bootstrap_name);
  char empy_script[] = { "script=" };
  char event_op[] = { "event" };

  stack_init(stack);
  stack_push(stack, mach_helper);
  stack_push(stack, empy_script);
  stack_push(stack, name);
  stack_push(stack, SET);
  stack_push(stack, event);
  stack_push(stack, event_op);
  stack_push(stack, ADD);
  sketchybar_call_log_and_cleanup(stack);

  int index = -1;
  for (int i = 0; i < g_callbacks.num_callbacks; i++) {
    if (strcmp(g_callbacks.callbacks[i]->name, name) == 0
        && strcmp(g_callbacks.callbacks[i]->event, event) == 0) {
      index = i;
      break;
    }
  }

  if (index < 0) {
    g_callbacks.callbacks = realloc(g_callbacks.callbacks,
                                    sizeof(struct callback*)
                                    * ++g_callbacks.num_callbacks);

    struct callback* callback = malloc(sizeof(struct callback));
    m_clone(callback->name, name);
    m_clone(callback->event, event);
    callback->callback_ref = callback_ref;
    g_callbacks.callbacks[g_callbacks.num_callbacks - 1] = callback;
  } else {
    g_callbacks.callbacks[index]->callback_ref = callback_ref;
  }

  stack = stack_create();
  stack_init(stack);
  stack_push(stack, event);
  stack_push(stack, name);
  stack_push(stack, SUBSCRIBE);

  sketchybar_call_log_and_cleanup(stack);
}

int subscribe(lua_State* state) {
  if (lua_gettop(state) < 3
      || lua_type(state, -1) != LUA_TFUNCTION
      || (lua_type(state, -2) != LUA_TSTRING
          && lua_type(state, -2) != LUA_TTABLE)) {
    char error[] = "[Lua] Error: expecting a string, a string or a table, "
                   "and a function as arguments for 'subscribe'";

    printf("%s\n", error);
    return 0;
  }

  const char* name = get_name_from_state(state);
  lua_pushvalue(state, -1);
  int callback_ref = luaL_ref(state, LUA_REGISTRYINDEX);
  lua_pop(state, 1);

  if (lua_type(state, 2) == LUA_TSTRING) {
    const char* event = lua_tostring(state, 2);
    subscribe_register_event(name, event, callback_ref);
  } else if (lua_type(state, 2) == LUA_TTABLE) {
    struct stack* stack = stack_create();
    stack_init(stack);
    parse_table_values_to_stack(state, 2, stack);
    for (int i = 0; i < stack->num_values; i++) {
      subscribe_register_event(name, stack->value[i], callback_ref);
    }
    stack_destroy(stack);
  }
  return 0;
}

int query(lua_State* state) {
  if (lua_gettop(state) < 1) {
    char error[] = "[Lua] Error: expecting a string or item as the only argument for 'query'";
    printf("%s\n", error);
    return 0;
  }

  const char* query;
  if (lua_type(state, 1) == LUA_TTABLE) {
    query = get_name_from_state(state);
  } else {
    query = lua_tostring(state, -1);
  }

  struct stack* stack = stack_create();
  stack_init(stack);
  stack_push(stack, query);
  stack_push(stack, QUERY);
  transaction_commit(state);
  char* response = sketchybar(stack);
  stack_destroy(stack);
  if (response) {
    json_to_lua_table(state, response);
    free(response);
    return 1;
  }

  return 0;
}
void generate_uid(char* buffer) {
  snprintf(buffer, 64, "item_%d", g_uid_counter++);
}

int push(lua_State *state) {
  // Ensure push is in a valid state:
  if (lua_gettop(state) < 2) {
    char error[] = "[Lua] Error: expecting at least two arguments for 'push'";
    printf("%s. Received %d\n", error, lua_gettop(state));
    return 0;
  }
  if (lua_type(state, 1) != LUA_TSTRING && lua_type(state, 1) != LUA_TTABLE) {
    char error[] = "[Lua] Error: expecting a 'string' or a 'table' as the first "
                   "argument for 'push'";
    printf("%s. Found '%s'\n", error, luat_to_string(lua_type(state, 1)));
    return 0;
  } else if (lua_type(state, 2) != LUA_TTABLE) {
    char error[] = "[Lua] Error: expecting a 'table' as the second argument for 'push'";
    printf("%s. Found '%s'\n", error, luat_to_string(lua_type(state, 2)));
  }

  // Technically this method will work regardless, so all we need to do 
  // is ensure a valid input state before we reach this part:
  struct stack *stack = stack_create();
  stack_init(stack);

  const char *name = get_name_from_state(state);
  parse_table_values_to_stack(state, 2, stack);

  stack_push(stack, name);
  stack_push(stack, PUSH);
  sketchybar_call_log_and_cleanup(stack);

  return 0;
}

int add(lua_State* state) {
  if (lua_gettop(state) < 2
      || lua_type(state, 1) != LUA_TSTRING) {
    char error[] = "[Lua] Error: expecting at least one string argument "
                   "for 'add'";
    printf("%s\n", error);
    return 0;
  }

  struct stack* stack = stack_create();
  stack_init(stack);
  const char* type = lua_tostring(state, 1);


  char name[64];
  if (lua_type(state, 2) == LUA_TSTRING) {
    snprintf(name, 64, "%s", lua_tostring(state, 2));
  } else {
    generate_uid(name);
    lua_pushstring(state, name);
    lua_insert(state, 2);
  }

  if (strcmp(type,"item") == 0
      || strcmp(type, "alias") == 0
      || strcmp(type, "space") == 0) {
    // "Regular" items with name and position
    const char* position = { "left" };
    stack_push(stack, position);
  } else if (strcmp(type, "event") == 0) {
    // Ensure event name is a string:
    if (lua_type(state, 2) != LUA_TSTRING) {
      char error[] = "[Lua] Error: expecting a 'string' as second argument"
                     " for 'add' when the type is 'event'";
      printf("%s\n", error);
      stack_destroy(stack);
      return 0;
    }

    // Check number of opts to see if we have an optional Notif String:
    if (lua_gettop(state) == 3) {
      // Check if the 3rd option is a string.
      if (lua_type(state, 3) != LUA_TSTRING) {
        char error[] = "[Lua] Error: expecting a 'string' as third argument"
                       " for 'add' when the type is 'event'";
        printf("%s\n", error);
        stack_destroy(stack);
        return 0;
      } else {
        // Else process DistributionNotification:
        stack_push(stack, lua_tostring(state, 3));
      }
    }
  } else if (strcmp(type, "slider") == 0) {
    if (lua_gettop(state) < 3) {
      char error[] = "[Lua] Error: expecting at least 3 arguments for 'add' when "
                     "the type is 'slider'";
      printf("%s. Recieved: %d\n", error, lua_gettop(state));
      stack_destroy(stack);
      return 0;
    } else if (lua_type(state, 3) != LUA_TNUMBER) {
      char error[] = "[Lua] Error: expecting a 'number' for the 3rd argument "
                     "'add' when type is 'slider'"; 
      printf("%s. Found %s\n", error, luat_to_string(lua_type(state, 3)));
      stack_destroy(stack);
      return 0;
    }

    // Push the slider width to the stack 
    stack_push(stack, lua_tostring(state, 3));

    // And the position as always.
    const char *position = { "left" };
    stack_push(stack, position);

  } else if (strcmp(type, "graph") == 0) {
    if (lua_gettop(state) < 3) {
      char error[] = "[Lua] Error: expecting at least 3 arguments for 'add' when "
                     "the type is 'graph'";
      printf("%s. Recieved: %d\n", error, lua_gettop(state));
      stack_destroy(stack);
      return 0;
    } else if (lua_type(state, 3) != LUA_TNUMBER) {
      char error[] = "[Lua] Error: expecting a 'number' for the 3rd argument "
                     "'add' when type is 'graph'"; 
      printf("%s. Found %s\n", error, luat_to_string(lua_type(state, 3)));
      stack_destroy(stack);
      return 0;
    } else if (lua_type(state, 4) != LUA_TTABLE) {
      char error[] = "[Lua] Error: expecting a 'table' for the 4th argument "
                     "'add' when type is 'graph'"; 
      printf("%s. Found %s\n", error, luat_to_string(lua_type(state, 3)));
      stack_destroy(stack);
      return 0;
    }
    
    // Push the width to the stack
    stack_push(stack, lua_tostring(state, 3));

    // Set the position for the graph by default:
    const char *position = { "left" };
    stack_push(stack, position);

  } else if (strcmp(type, "bracket") == 0) {
    // A bracket takes a list of member items instead of a position
    if (lua_type(state, 3) != LUA_TTABLE) {
      char error[] = "[Lua] Error: expecting a lua table as third argument"
                     " for 'add', when the type is 'bracket'";
      printf("%s\n", error);
      stack_destroy(stack);
      return 0;
    }

    lua_pushnil(state);
    while (lua_next(state, 3)) {
      const char* member = lua_tostring(state, -1);
      stack_push(stack, member);
      lua_pop(state, 1);
    }
  } else {
    char error[] = "[Lua] Error: Item type not supported yet, create case for"
                    " it in src/sketchybar.c";
    printf("%s\n", error);
    stack_destroy(stack);
    return 0;
  }

  stack_push(stack, name);
  stack_push(stack, type);
  stack_push(stack, ADD);
  sketchybar_call_log_and_cleanup(stack);

  // If a table is presented as the last argument, we parse it as if it
  // was passed to the set domain.
  if (lua_type(state, -1) == LUA_TTABLE) {
    lua_pushstring(state, name);
    lua_insert(state, -2);
    while (lua_gettop(state) > 2) { lua_remove(state, -3); }
    set(state);
  }

  // Return the item element as a table
  lua_newtable(state);
  lua_pushstring(state, "name");
  lua_pushstring(state, name);
  lua_settable(state,-3);
  lua_pushstring(state, "set");
  lua_pushcfunction(state, set);
  lua_settable(state,-3);
  lua_pushstring(state, "subscribe");
  lua_pushcfunction(state, subscribe);
  lua_settable(state,-3);
  lua_pushstring(state, "query");
  lua_pushcfunction(state, query);
  lua_settable(state,-3);
  lua_pushstring(state, "push");
  lua_pushcfunction(state, push);
  lua_settable(state,-3);
  return 1;
}

int remove_sbar(lua_State* state) {
  if (lua_gettop(state) < 1) {
    char error[] = "[Lua] Error: expecting at least one argument "
                   "for 'remove'";
    printf("%s\n", error);
    return 0;
  }

  const char* name = get_name_from_state(state);
  struct stack* stack = stack_create();
  stack_init(stack);
  stack_push(stack, name);
  stack_push(stack, REMOVE);
  sketchybar_call_log_and_cleanup(stack);
  return 0;}

static void orphan_check() {
  if (getppid() == 1) exit(0);
}

int event_loop(lua_State* state) {
  g_state = state;
  struct stack* stack = stack_create();
  stack_init(stack);
  stack_push(stack, UPDATE);
  sketchybar_call_log_and_cleanup(stack);
  alarm(0);
  mach_server_begin(&g_mach_server, callback_function);
  CFRunLoopTimerRef orphan_timer
     = CFRunLoopTimerCreate(kCFAllocatorDefault,
                            CFAbsoluteTimeGetCurrent() + 1.0,
                            1.0,
                            0,
                            0,
                            orphan_check,
                            NULL                             );
  CFRunLoopAddTimer(CFRunLoopGetMain(), orphan_timer, kCFRunLoopDefaultMode);
  CFRelease(orphan_timer);
  CFRunLoopRun();
  return 0;
}

int hotload(lua_State* state) {
  if (lua_gettop(state) != 1
      || !lua_isboolean(state, 1)) {
    char error[] = "[Lua] Error: expecting a boolean as the only argument for 'hotload'";
    printf("%s\n", error);
    return 0;
  }
  g_state = state;
  struct stack* stack = stack_create();
  stack_init(stack);
  if (lua_toboolean(state, 1)) {
    stack_push(stack, "on");
  } else {
    stack_push(stack, "off");
  }
  stack_push(stack, HOTLOAD);
  sketchybar_call_log_and_cleanup(stack);
  return 0;
}

int trigger(lua_State *state) {
  // Check for event name:
  if (lua_gettop(state) < 1 || lua_type(state, 1) != LUA_TSTRING) {
    char error[] = "[Lua] Error: expecting at least one string as an argument "
                   "for 'trigger'";
    printf("%s\n", error);
    return 0;
  }

  // Push the event name onto the stack:
  struct stack *stack = stack_create();
  stack_init(stack);

  const char* event = lua_tostring(state, 1);

  if (lua_gettop(state) > 1 && lua_type(state, 2) != LUA_TTABLE) {
    char error[] = "[Lua] Error: expecting a table as the second argument for "
                   "'trigger'";
    printf("%s\n", error);
    stack_destroy(stack);
    return 0;
  } else if (lua_gettop(state) > 1) {
    // Parse potential ENV variables onto stack:
    parse_kv_table(state, NULL, stack);
  }

  // No errors, lets parse the trigger state:
  stack_push(stack, event);
  stack_push(stack, TRIGGER);
  sketchybar_call_log_and_cleanup(stack);

  return 0;
}


int set_bar_name(lua_State* state) {
  if (lua_gettop(state) < 1
      || lua_type(state, 1) != LUA_TSTRING) {
    char error[] = "[Lua] Error: expecting a string argument "
                   "for 'set_bar_name'";
    printf("%s\n", error);
    return 0;
  }

  const char* name = lua_tostring(state, 1);
  g_port = 0;
  snprintf(g_bs_lookup, 256, "git.felix.%s", name);
  return 0;
}

int exec(lua_State* state) {
  if (lua_gettop(state) < 1
      || lua_type(state, 1) != LUA_TSTRING) {
    char error[] = "[Lua] Error: expecting a string as first argument "
                   "for 'exec'";
    printf("%s\n", error);
    return 0;
  }

  int callback_ref = 0;
  if (lua_gettop(state) > 1 && lua_type(state, 2) == LUA_TFUNCTION) {
    lua_pushvalue(state, 2);
    callback_ref = luaL_ref(state, LUA_REGISTRYINDEX);
  }
  const char* command = lua_tostring(state, 1);

  int pid = fork();
  if (pid != 0) return 0;

  alarm(60);
  if (!callback_ref) {
    char *exec[] = { "/usr/bin/env", "sh", "-c", (char*)command, NULL };
    exit(execvp(exec[0], exec));
  } else {
    FILE* file = popen(command, "r");
    char* result = malloc(1024);
    size_t read_bytes = 0;
    size_t total_bytes = 0;
    while ((read_bytes = fread(result + total_bytes, 1, 1024, file)) > 0) {
      total_bytes += read_bytes;
      result = realloc(result, total_bytes + 1024);
    }
    size_t message_size = total_bytes + sizeof(int) + sizeof(char);
    char message[message_size];
    message[0] = '\x07';
    memcpy(message + 1, &callback_ref, sizeof(int));
    memcpy(message + 1 + sizeof(int), result, total_bytes);

    mach_send_message(mach_get_bs_port(g_bootstrap_name),
                      message,
                      message_size,
                      false                              );

    free(result);
    exit(pclose(file));
  }
}

void delay_callback(CFRunLoopTimerRef timer, void* context) {
  int callback_ref = (int)(intptr_t)context;

  lua_rawgeti(g_state, LUA_REGISTRYINDEX, callback_ref);
  transaction_create(g_state);
  int error = lua_pcall(g_state, 0, 0, 0);

  if (error && lua_gettop(g_state)) {
    printf("[!] Lua: %s\n", lua_tostring(g_state, -1));
  }

  transaction_commit(g_state);
}

int delay(lua_State* state) {
  if (lua_gettop(state) < 2
      || lua_type(state, 1) != LUA_TNUMBER
      || lua_type(state, 2) != LUA_TFUNCTION) {
    char error[] = "[Lua] Error: expecting a number and a function as"
                   " arguments for 'delay'";
    printf("%s\n", error);
    return 0;
  }

  lua_pushvalue(state, 2);
  int callback_ref = luaL_ref(state, LUA_REGISTRYINDEX);

  lua_gettop(state);

  double duration = lua_tonumber(state, 1);
  CFRunLoopTimerContext context = { 0 };
  context.info = (void*)(intptr_t)callback_ref;
  CFRunLoopTimerRef timer
     = CFRunLoopTimerCreate(kCFAllocatorDefault,
                            CFAbsoluteTimeGetCurrent() + duration,
                            0,
                            0,
                            0,
                            delay_callback,
                            &context                              );

  CFRunLoopAddTimer(CFRunLoopGetMain(), timer, kCFRunLoopDefaultMode);
  CFRelease(timer);
  return 0;
}

static const struct luaL_Reg functions[] = {
    { "add", add },
    { "remove", remove_sbar },
    { "set", set },
    { "bar", bar },
    { "default", defaults },
    { "animate", animate },
    { "subscribe", subscribe },
    { "query", query },
    { "event_loop", event_loop },
    { "hotload", hotload },
    { "set_bar_name", set_bar_name },
    { "trigger", trigger },
    { "push", push},
    { "exec", exec },
    { "delay", delay },
    { "begin_config", transaction_create },
    { "end_config", transaction_commit },
    {NULL, NULL}
};

int luaopen_sketchybar(lua_State* L) {
  memset(&g_callbacks, 0, sizeof(g_callbacks));
  snprintf(g_bootstrap_name, sizeof(g_bootstrap_name), MACH_HELPER_FMT,
                                                       (int)(intptr_t)L);

  mach_server_register(&g_mach_server, g_bootstrap_name);
  luaL_newlib(L, functions);

  lua_pushcfunction(L, subscribe);
  lua_setfield(L, -2, "subscribe");

  lua_pushcfunction(L, animate);
  lua_setfield(L, -2, "animate");

  lua_pushcfunction(L, defaults);
  lua_setfield(L, -2, "default");

  lua_pushcfunction(L, bar);
  lua_setfield(L, -2, "bar");

  lua_pushcfunction(L, set);
  lua_setfield(L, -2, "set");

  lua_pushcfunction(L, add);
  lua_setfield(L, -2, "add");

  lua_pushcfunction(L, remove_sbar);
  lua_setfield(L, -2, "remove");

  lua_pushcfunction(L, event_loop);
  lua_setfield(L, -2, "update");

  lua_pushcfunction(L, hotload);
  lua_setfield(L, -2, "hotload");

  lua_pushcfunction(L, trigger);
  lua_setfield(L, -2, "trigger");

  lua_pushcfunction(L, push);
  lua_setfield(L, -2, "push");

  lua_pushcfunction(L, exec);
  lua_setfield(L, -2, "exec");

  lua_pushcfunction(L, delay);
  lua_setfield(L, -2, "delay");

  lua_pushcfunction(L, transaction_create);
  lua_setfield(L, -2, "begin_config");

  lua_pushcfunction(L, transaction_commit);
  lua_setfield(L, -2, "end_config");

  return 1;
}
