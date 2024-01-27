/*
   @title     StarMod
   @file      SysModModel.h
   @date      20240114
   @repo      https://github.com/ewowi/StarMod
   @Authors   https://github.com/ewowi/StarMod/commits/main
   @Copyright (c) 2024 Github StarMod Commit Authors
   @license   GNU GENERAL PUBLIC LICENSE Version 3, 29 June 2007
   @license   For non GPL-v3 usage, commercial licenses must be purchased. Contact moonmodules@icloud.com
*/

#pragma once
#include "SysModule.h"
#include "SysModPrint.h"
#include "SysModUI.h"
#include "SysModWeb.h"

#include "ArduinoJson.h"

typedef std::function<void(JsonObject)> FindFun;
typedef std::function<void(JsonObject, size_t)> ChangeFun;

struct Coord3D {
  uint16_t x;
  uint16_t y;
  uint16_t z;

  // Coord3D() {
  //   x = 0;
  //   y = 0;
  //   z = 0;
  // }
  // Coord3D(uint16_t x, uint16_t y, uint16_t z) {
  //   this->x = x;
  //   this->y = y;
  //   this->z = y;
  // }

  bool operator!=(Coord3D rhs) {
    // USER_PRINTF("Coord3D compare%d %d %d %d %d %d\n", x, y, z, rhs.x, rhs.y, rhs.z);
    return x != rhs.x || y != rhs.y || z != rhs.z;
  }
  bool operator==(Coord3D rhs) {
    return x == rhs.x && y == rhs.y && z == rhs.z;
  }
  Coord3D operator=(Coord3D rhs) {
    USER_PRINTF("Coord3D assign %d %d %d\n", rhs.x, rhs.y, rhs.z);
    this->x = rhs.x;
    this->y = rhs.y;
    this->z = rhs.z;
    return *this;
  }
};

//used to sort keys of jsonobjects
struct ArrayIndexSortValue {
  size_t index;
  uint32_t value;
};

//https://arduinojson.org/news/2021/05/04/version-6-18-0/
namespace ArduinoJson {
  template <>
  struct Converter<Coord3D> {
    static bool toJson(const Coord3D& src, JsonVariant dst) {
      dst["x"] = src.x;
      dst["y"] = src.y;
      dst["z"] = src.z;
      USER_PRINTF("dest %d,%d,%d -> %s ", src.x, src.y, src.z, dst.as<String>().c_str());
      return true;
    }

    static Coord3D fromJson(JsonVariantConst src) {
      return Coord3D{src["x"], src["y"], src["z"]};
    }

    static bool checkJson(JsonVariantConst src) {
      return src["x"].is<uint16_t>() && src["y"].is<uint16_t>() && src["z"].is<uint16_t>();
    }
  };
}

// https://arduinojson.org/v6/api/basicjsondocument/
struct RAM_Allocator {
  void* allocate(size_t size) {
    if (psramFound()) return ps_malloc(size); // use PSRAM if it exists
    else              return malloc(size);    // fallback
    // return heap_caps_malloc(size, MALLOC_CAP_SPIRAM);
  }
  void deallocate(void* pointer) {
    free(pointer);
    // heap_caps_free(pointer);
  }
  void* reallocate(void* ptr, size_t new_size) {
    if (psramFound()) return ps_realloc(ptr, new_size); // use PSRAM if it exists
    else              return realloc(ptr, new_size);    // fallback
    // return heap_caps_realloc(ptr, new_size, MALLOC_CAP_SPIRAM);
  }
};



class SysModModel:public SysModule {

public:

  // StaticJsonDocument<24576> model; //not static as that blows up the stack. Use extern??
  // static BasicJsonDocument<DefaultAllocator> *model; //needs to be static as loopTask and asyncTask is using it...
  static BasicJsonDocument<RAM_Allocator> *model; //needs to be static as loopTask and asyncTask is using it...

  static JsonObject modelParentVar;

  bool doWriteModel = false;

  SysModModel();
  void setup();
  void loop();
  void loop1s();

  //scan all vars in the model and remove vars where var["o"] is negative or positive, if ro then remove ro values
  void cleanUpModel(JsonArray vars, bool oPos = true, bool ro = false);

  //sets the value of var with id
  template <typename Type>
  JsonObject setValue(const char * id, Type value, uint8_t rowNr = UINT8_MAX) {
    JsonObject var = findVar(id);
    if (!var.isNull()) {
      return setValue(var, value, rowNr);
    }
    else {
      USER_PRINTF("setValue Var %s not found\n", id);
      return JsonObject();
    }
  }

  template <typename Type>
  JsonObject setValue(JsonObject var, Type value, uint8_t rowNr = UINT8_MAX) {
    // print->printJson("setValueB", var);
    if (rowNr == UINT8_MAX) { //normal situation
      if (var["ro"].as<bool>()) {
        //read only vars not in the model (tbd: modify setChFunAndWs)
        JsonDocument *responseDoc = web->getResponseDoc();
        responseDoc->clear(); //needed for deserializeJson?
        JsonVariant responseVariant = responseDoc->as<JsonVariant>();

        web->addResponse(var["id"], "value", value);

        web->sendDataWs(responseVariant);
      }
      else if (var["value"].isNull() || var["value"].as<Type>() != value) { //const char * will be JsonString so comparison works
        USER_PRINTF("setValue changed %s[%d] %s->", var["id"].as<const char *>(), rowNr, var["value"].as<String>().c_str()); //old value
        var["value"] = value;
        USER_PRINTF("%s\n", var["value"].as<String>().c_str()); //newvalue
        ui->setChFunAndWs(var, rowNr);
      }
    }
    else {
      //if we deal with multiple rows, value should be an array, if not we create one

      if (var["value"].isNull() || !var["value"].is<JsonArray>()) {
        USER_PRINTF("setValue var %s[%d] value %s not array, creating\n", var["id"].as<const char *>(), rowNr, var["value"].as<String>().c_str());
        // print->printJson("setValueB var %s value %s not array, creating", id, var["value"].as<String>().c_str());
        var.createNestedArray("value");
      }

      if (var["value"].is<JsonArray>()) {
        JsonArray valueArray = var["value"].as<JsonArray>();
        //set the right value in the array (if array did not contain values yet, all values before rownr are set to false)
        bool notSame = true;

        if (rowNr < valueArray.size())
          notSame = valueArray[rowNr].isNull() || valueArray[rowNr].as<Type>() != value;

        if (notSame) {
          valueArray[rowNr] = value; //if valueArray[rowNr] not exists it will be created
          ui->setChFunAndWs(var, rowNr);
        }
      }
      else {
        USER_PRINTF("setValue %s could not create value array\n", var["id"].as<const char *>());
      }
    }
    
    return var;
  }

  //Set value with argument list
  // JsonObject setValueV(const char * id, uint8_t rowNr = UINT8_MAX, const char * format, ...) {
  //   setValueV(id, UINT8_MAX, format);
  // }

  JsonObject setValueV(const char * id, const char * format, ...) {
    va_list args;
    va_start(args, format);

    char value[128];
    vsnprintf(value, sizeof(value)-1, format, args);

    va_end(args);

    return setValue(id, JsonString(value, JsonString::Copied));
  }

  JsonObject setValueP(const char * id, const char * format, ...) {
    va_list args;
    va_start(args, format);

    char value[128];
    vsnprintf(value, sizeof(value)-1, format, args);
    USER_PRINTF("%s\n", value);

    va_end(args);

    return setValue(id, JsonString(value, JsonString::Copied));
  }

  JsonVariant getValue(const char * id, uint8_t rowNr = UINT8_MAX) {
    JsonObject var = findVar(id);
    if (!var.isNull()) {
      return getValue(var, rowNr);
    }
    else {
      USER_PRINTF("getValue: Var %s does not exist!!\n", id);
      return JsonVariant();
    }
  }
  JsonVariant getValue(JsonObject var, uint8_t rowNr = UINT8_MAX) {
    if (var["value"].is<JsonArray>()) {
      JsonArray valueArray = var["value"].as<JsonArray>();
      if (rowNr != UINT8_MAX && rowNr < valueArray.size())
        return valueArray[rowNr];
      else {
        USER_PRINTF("perror getValue no array or rownr wrong %s %d\n", var["value"].as<String>().c_str(), rowNr);
        return JsonVariant();
      }
    }
    else
      return var["value"];
  }

  //returns the var defined by id (parent to recursively call findVar)
  static JsonObject findVar(const char * id, JsonArray parent = JsonArray()); //static for processJson
  void findVars(const char * id, bool value, FindFun fun, JsonArray parent = JsonArray());

  //recursively add values in  a variant
  void varToValues(JsonObject var, JsonArray values);

private:
  bool doShowObsolete = false;
  bool cleanUpModelDone = false;

};

static SysModModel *mdl;