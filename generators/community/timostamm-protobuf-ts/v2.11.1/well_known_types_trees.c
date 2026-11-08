/* Copyright (c) 2016 Timo Stamm
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License. */

/* The method texts below are from packages/plugin/src/message-type-extensions/
 * well-known-types.ts and google-types.ts of @protobuf-ts/plugin 2.11.1. The tables after them
 * encode the trees TypeScript 3.9.10 ts.createSourceFile parses from those texts, in the format
 * of ts_decode_parsed (pkg/languages/ecmascript/formatters/typescript/decode.c). They were
 * produced once with a port of the 3.9.10 scanner and parser, which is no longer in the tree
 * (the printer does not need a front end); the wkt golden test checks every handler against the
 * upstream output, and a change to a text means editing its encoding by hand following decode.c.
 * ${...} of upstream is $$<n>$$, the n-th parameter of the handler (S: the message, T/N: an
 * import (type-only / not), L: a field name, C: the long conversion method, E: the LongType
 * member). A JSDoc text precedes each function as a JSDocComment node with the text. */

// google.protobuf.Any (S,T:IMessageType,T:BinaryReadOptions,T:JsonWriteOptions,T:JsonReadOptions,N:jsonWriteOptions,T:JsonValue,N:typeofJsonValue,N:isJsonObject,L:type_url)
//
//   /**
//   * Pack the message into a new `Any`.
//   *
//   * Uses 'type.googleapis.com/full.type.name' as the type URL.
//   */
//   function pack<T extends object>(message: T, type: $$1$$<T>): $$0$$ {
//   return {
//   $$9$$: this.typeNameToUrl(type.typeName),
//   value: type.toBinary(message),
//   };
//   }
//
//   /**
//   * Unpack the message from the `Any`.
//   */
//   function unpack<T extends object>(any: $$0$$, type: $$1$$<T>, options?: Partial<$$2$$>): T {
//   if (!this.contains(any, type))
//   throw new Error("Cannot unpack google.protobuf.Any with $$9$$ '" + any.$$9$$ + "' as " + type.typeName + ".");
//   return type.fromBinary(any.value, options);
//   }
//
//   /**
//   * Does the given `Any` contain a packed message of the given type?
//   */
//   function contains(any: $$0$$, type: $$1$$<any> | string): boolean {
//   if (!any.$$9$$.length)
//   return false;
//   let wants = typeof type == "string" ? type : type.typeName;
//   let has = this.typeUrlToName(any.$$9$$);
//   return wants === has;
//   }
//
//   /**
//   * Convert the message to canonical JSON value.
//   *
//   * You have to provide the `typeRegistry` option so that the
//   * packed message can be converted to JSON.
//   *
//   * The `typeRegistry` option is also required to read
//   * `google.protobuf.Any` from JSON format.
//   */
//   function internalJsonWrite(any: $$0$$, options: $$3$$): $$6$$ {
//   if (any.$$9$$ === "")
//   return {};
//   let typeName = this.typeUrlToName(any.$$9$$);
//   let opt = $$5$$(options);
//   let type = opt.typeRegistry?.find(t => t.typeName === typeName);
//   if (!type)
//   throw new globalThis.Error("Unable to convert google.protobuf.Any with typeUrl '" + any.$$9$$ + "' to JSON. The specified type " + typeName + " is not available in the type registry.");
//   let value = type.fromBinary(any.value, {readUnknownField: false});
//   let json = type.internalJsonWrite(value, opt);
//   if (typeName.startsWith("google.protobuf.") || !$$8$$(json))
//   json = {value: json};
//   json['@type'] = any.$$9$$;
//   return json;
//   }
//
//   function internalJsonRead(json: $$6$$, options: $$4$$, target?: $$0$$): $$0$$ {
//   if (!$$8$$(json))
//   throw new globalThis.Error("Unable to parse google.protobuf.Any from JSON " + $$7$$(json) + ".");
//   if (typeof json['@type'] != "string" || json['@type'] == '')
//   return this.create();
//   let typeName = this.typeUrlToName(json["@type"]);
//   let type = options?.typeRegistry?.find(t => t.typeName == typeName);
//   if (!type)
//   throw new globalThis.Error("Unable to parse google.protobuf.Any from JSON. The specified type " + typeName + " is not available in the type registry.");
//   let value;
//   if (typeName.startsWith('google.protobuf.') && json.hasOwnProperty('value'))
//   value = type.fromJson(json['value'], options);
//   else {
//   let copy = Object.assign({}, json);
//   delete copy['@type'];
//   value = type.fromJson(copy, options);
//   }
//   if (target === undefined)
//   target = this.create();
//   target.$$9$$ = json['@type'];
//   target.value = type.toBinary(value);
//   return target;
//   }
//
//   function typeNameToUrl(name: string): string {
//   if (!name.length)
//   throw new Error('invalid type name: ' + name);
//   return "type.googleapis.com/" + name;
//   }
//
//   function typeUrlToName(url: string): string {
//   if (!url.length)
//   throw new Error('invalid type url: ' + url);
//   let slash = url.lastIndexOf("/");
//   let name = slash > 0 ? url.substring(slash + 1) : url;
//   if (!name.length)
//   throw new Error('invalid type url: ' + url);
//   return name;
//   }

// google.protobuf.Timestamp (S,N:PbLong,T:JsonWriteOptions,T:JsonReadOptions,T:JsonValue,N:typeofJsonValue,C)
//
//   /**
//   * Creates a new `Timestamp` for the current time.
//   */ 
//   function now(): $$0$$ {
//   const msg = this.create();
//   const ms = Date.now();
//   msg.seconds = $$1$$.from(Math.floor(ms / 1000)).$$6$$();
//   msg.nanos = (ms % 1000) * 1000000;
//   return msg;
//   }
//
//   /**
//   * Converts a `Timestamp` to a JavaScript Date.
//   */ 
//   function toDate(message: $$0$$): Date {
//   return new Date($$1$$.from(message.seconds).toNumber() * 1000 + Math.ceil(message.nanos / 1000000));
//   }
//
//   /**
//   * Converts a JavaScript Date to a `Timestamp`.
//   */ 
//   function fromDate(date: Date): $$0$$ {
//   const msg = this.create();
//   const ms = date.getTime();
//   msg.seconds = PbLong.from(Math.floor(ms / 1000)).$$6$$();
//   msg.nanos = ((ms % 1000) + (ms < 0 && ms % 1000 !== 0 ? 1000 : 0)) * 1000000;
//   return msg;
//   }
//
//   /**
//   * In JSON format, the `Timestamp` type is encoded as a string 
//   * in the RFC 3339 format.
//   */
//   function internalJsonWrite(message: $$0$$, options: $$2$$): $$4$$ {
//   let ms = PbLong.from(message.seconds).toNumber() * 1000;
//   if (ms < Date.parse("0001-01-01T00:00:00Z") || ms > Date.parse("9999-12-31T23:59:59Z"))
//   throw new Error("Unable to encode Timestamp to JSON. Must be from 0001-01-01T00:00:00Z to 9999-12-31T23:59:59Z inclusive.");
//   if (message.nanos < 0)
//   throw new Error("Unable to encode invalid Timestamp to JSON. Nanos must not be negative.");
//   let z = "Z";
//   if (message.nanos > 0) {
//   let nanosStr = (message.nanos + 1000000000).toString().substring(1);
//   if (nanosStr.substring(3) === "000000")
//   z = "." + nanosStr.substring(0, 3) + "Z";
//   else if (nanosStr.substring(6) === "000")
//   z = "." + nanosStr.substring(0, 6) + "Z";
//   else
//   z = "." + nanosStr + "Z";
//   }
//   return new Date(ms).toISOString().replace(".000Z", z)
//   }
//
//   /**
//   * In JSON format, the `Timestamp` type is encoded as a string 
//   * in the RFC 3339 format.
//   */
//   function internalJsonRead(json: $$4$$, options: $$3$$, target?: $$0$$): $$0$$ {
//   if (typeof json !== "string")
//   throw new Error("Unable to parse Timestamp from JSON " + $$5$$(json) + ".");
//   // RFC 3339 with "Z" (UTC) or offset like "+08:00" and optional fractions
//   // match[7] optional fractions, 3 to 9 digits
//   // match[8] offset like "-08:00", if undefined date is UTC
//   let matches = json.match(/^([0-9]{4})-([0-9]{2})-([0-9]{2})T([0-9]{2}):([0-9]{2}):([0-9]{2})(?:Z|\.([0-9]{3,9})Z|([+-][0-9][0-9]:[0-9][0-9]))$/);
//   if (!matches)
//   throw new Error("Unable to parse Timestamp from JSON. Invalid format.");
//   let ms = Date.parse(matches[1] + "-" + matches[2] + "-" + matches[3] + "T" + matches[4] + ":" + matches[5] + ":" + matches[6] + (matches[8] ? matches[8] : "Z"));
//   if (Number.isNaN(ms))
//   throw new Error("Unable to parse Timestamp from JSON. Invalid value.");
//   if (ms < Date.parse("0001-01-01T00:00:00Z") || ms > Date.parse("9999-12-31T23:59:59Z"))
//   throw new globalThis.Error("Unable to parse Timestamp from JSON. Must be from 0001-01-01T00:00:00Z to 9999-12-31T23:59:59Z inclusive.");
//   if (!target)
//   target = this.create();
//   target.seconds = PbLong.from(ms / 1000).$$6$$();
//   target.nanos = 0;
//   if (matches[7])
//   target.nanos = (parseInt("1" + matches[7] + "0".repeat(9 - matches[7].length)) - 1000000000);
//   return target;
//   }

// google.protobuf.Duration (S,N:PbLong,T:JsonWriteOptions,T:JsonReadOptions,T:JsonValue,N:typeofJsonValue,C)
//
//   /**
//   * Encode `Duration` to JSON string like "3.000001s". 
//   */
//   function internalJsonWrite(message: $$0$$, options: $$2$$): $$4$$ {
//   let s = $$1$$.from(message.seconds).toNumber();
//   if (s > 315576000000 || s < -315576000000)
//   throw new Error("Duration value out of range.");
//   let text = message.seconds.toString();
//   if (s === 0 && message.nanos < 0)
//   text = "-" + text;
//   if (message.nanos !== 0) {
//   let nanosStr = Math.abs(message.nanos).toString();
//   nanosStr = "0".repeat(9 - nanosStr.length) + nanosStr;
//   if (nanosStr.substring(3) === "000000")
//   nanosStr = nanosStr.substring(0, 3);
//   else if (nanosStr.substring(6) === "000")
//   nanosStr = nanosStr.substring(0, 6);
//   text += "." + nanosStr;
//   }
//   return text + "s";
//   }
//
//   /**
//   * Decode `Duration` from JSON string like "3.000001s"
//   */
//   function internalJsonRead(json: $$4$$, options: $$3$$, target?: $$0$$): $$0$$ {
//   if (typeof json !== "string")
//   throw new Error("Unable to parse Duration from JSON " + $$5$$(json) + ". Expected string.");
//   let match = json.match(/^(-?)([0-9]+)(?:\.([0-9]+))?s/);
//   if (match === null)
//   throw new Error("Unable to parse Duration from JSON string. Invalid format.");
//   if (!target)
//   target = this.create();
//   let [, sign, secs, nanos] = match;
//   let longSeconds = $$1$$.from(sign + secs);
//   if (longSeconds.toNumber() > 315576000000 || longSeconds.toNumber() < -315576000000)
//   throw new Error("Unable to parse Duration from JSON string. Value out of range.");
//   target.seconds = longSeconds.$$6$$();
//   if (typeof nanos == "string") {
//   let nanosStr = sign + nanos + "0".repeat(9 - nanos.length);
//   target.nanos = parseInt(nanosStr);
//   }
//   return target;
//   }

// google.protobuf.FieldMask (S,T:JsonWriteOptions,T:JsonReadOptions,N:lowerCamelCase,T:JsonValue,N:typeofJsonValue)
//
//   /**
//   * Encode `FieldMask` to JSON object. 
//   */
//   function internalJsonWrite(message: $$0$$, options: $$1$$): $$4$$ {
//   const invalidFieldMaskJsonRegex = /[A-Z]|(_([.0-9_]|$))/g;
//   return message.paths.map(p => {
//   if (invalidFieldMaskJsonRegex.test(p))
//   throw new Error("Unable to encode FieldMask to JSON. lowerCamelCase of path name \""+p+"\" is irreversible.");
//   return $$3$$(p);
//   }).join(",");
//   }
//
//   /**
//   * Decode `FieldMask` from JSON object.
//   */
//   function internalJsonRead(json: $$4$$, options: $$2$$, target?: $$0$$): $$0$$ {
//   if (typeof json !== "string")
//   throw new Error("Unable to parse FieldMask from JSON " + $$5$$(json) + ". Expected string.");
//   if (!target)
//   target = this.create();
//   if (json === "")
//   return target;
//   let camelToSnake = (str: string) => {
//   if (str.includes('_'))
//   throw new Error("Unable to parse FieldMask from JSON. Path names must be lowerCamelCase.");
//   let sc = str.replace(/[A-Z]/g, letter => "_" + letter.toLowerCase());
//   return sc;
//   };
//   target.paths = json.split(",").map(camelToSnake);
//   return target;
//   }

// google.protobuf.Struct (S,T:JsonObject,T:JsonWriteOptions,T:JsonReadOptions,T:JsonValue,N:typeofJsonValue,N:isJsonObject)
//
//   /**
//   * Encode `Struct` to JSON object. 
//   */
//   function internalJsonWrite(message: $$0$$, options: $$2$$): $$4$$ {
//   let json: $$1$$ = {};
//   for (let [k, v] of Object.entries(message.fields)) {
//   json[k] = Value.toJson(v);
//   }
//   return json;
//   }
//
//   /**
//   * Decode `Struct` from JSON object.
//   */
//   function internalJsonRead(json: $$4$$, options: $$3$$, target?: $$0$$): $$0$$ {
//   if (!$$6$$(json))
//   throw new globalThis.Error("Unable to parse message " + this.typeName + " from JSON " + $$5$$(json) + ".");
//   if (!target) 
//   target = this.create();
//   for (let [k, v] of globalThis.Object.entries(json)) {
//   target.fields[k] = Value.fromJson(v);
//   }
//   return target;
//   }

// google.protobuf.Value (S,T:JsonWriteOptions,T:JsonReadOptions,T:JsonValue,N:typeofJsonValue,L:null_value,L:bool_value,L:number_value,L:string_value,L:list_value,L:struct_value)
//
//   /**
//   * Encode `Value` to JSON value. 
//   */
//   function internalJsonWrite(message: $$0$$, options: $$1$$): $$3$$ {
//   if (message.kind.oneofKind === undefined) throw new globalThis.Error();
//   switch (message.kind.oneofKind) {
//   case undefined:
//   throw new globalThis.Error();
//   case "$$6$$":
//   return message.kind.$$6$$;
//   case "$$5$$":
//   return null;
//   case "$$7$$":
//   let numberValue = message.kind.$$7$$;
//   if (typeof numberValue == "number" && !Number.isFinite(numberValue)) throw new globalThis.Error();
//   return numberValue;
//   case "$$8$$":
//   return message.kind.$$8$$;
//   case "$$9$$":
//   let listValueField = this.fields.find(f => f.no === 6);
//   if (listValueField?.kind !== 'message') throw new globalThis.Error();
//   return listValueField.T().toJson(message.kind.$$9$$);
//   case "$$10$$":
//   let structValueField = this.fields.find(f => f.no === 5);
//   if (structValueField?.kind !== 'message') throw new globalThis.Error();
//   return structValueField.T().toJson(message.kind.$$10$$);
//   }
//   }
//
//   /**
//   * Decode `Value` from JSON value.
//   */
//   function internalJsonRead(json: $$3$$, options: $$2$$, target?: $$0$$): $$0$$ {
//   if (!target) 
//   target = this.create();
//   switch (typeof json) {
//   case "number":
//   target.kind = {oneofKind: "$$7$$", $$7$$: json};
//   break;
//   case "string":
//   target.kind = {oneofKind: "$$8$$", $$8$$: json};
//   break;
//   case "boolean":
//   target.kind = {oneofKind: "$$6$$", $$6$$: json};
//   break;
//   case "object":
//   if (json === null) {
//   target.kind = {oneofKind: "$$5$$", $$5$$: NullValue.NULL_VALUE};
//   } else if (globalThis.Array.isArray(json)) {
//   target.kind = {oneofKind: "$$9$$", $$9$$: ListValue.fromJson(json)};
//   } else {
//   target.kind = {oneofKind: "$$10$$", $$10$$: Struct.fromJson(json)};
//   }
//   break;
//   default:
//   throw new globalThis.Error('Unable to parse ' + this.typeName + ' from JSON ' + $$4$$(json));
//   }
//   return target;
//   }

// google.protobuf.ListValue (S,T:JsonWriteOptions,T:JsonReadOptions,T:JsonValue,N:typeofJsonValue)
//
//   /**
//   * Encode `ListValue` to JSON array. 
//   */
//   function internalJsonWrite(message: $$0$$, options: $$1$$): $$3$$ {
//   return message.values.map(v => Value.toJson(v));
//   }
//
//   /**
//   * Decode `ListValue` from JSON array.
//   */
//   function internalJsonRead(json: $$3$$, options: $$2$$, target?: $$0$$): $$0$$ {
//   if (! globalThis.Array.isArray(json)) throw new globalThis.Error('Unable to parse ' + this.typeName + ' from JSON ' + $$4$$(json));
//   if (!target) 
//   target = this.create();
//   let values = json.map(v => Value.fromJson(v));
//   target.values.push(...values);
//   return target;
//   }

// google.protobuf.BoolValue (S,T:JsonWriteOptions,T:JsonReadOptions,T:JsonValue)
//
//   /**
//   * Encode `BoolValue` to JSON bool. 
//   */
//   function internalJsonWrite(message: $$0$$, options: $$1$$): $$3$$ {
//   return message.value;
//   }
//
//   /**
//   * Decode `BoolValue` from JSON bool.
//   */
//   function internalJsonRead(json: $$3$$, options: $$2$$, target?: $$0$$): $$0$$ {
//   if (!target) 
//   target = this.create();
//   target.value = this.refJsonReader.scalar(json, 8, undefined, "value") as boolean;
//   return target;
//   }

// google.protobuf.StringValue (S,T:JsonWriteOptions,T:JsonReadOptions,T:JsonValue)
//
//   /**
//   * Encode `StringValue` to JSON string. 
//   */
//   function internalJsonWrite(message: $$0$$, options: $$1$$): $$3$$ {
//   return message.value;
//   }
//
//   /**
//   * Decode `StringValue` from JSON string.
//   */
//   function internalJsonRead(json: $$3$$, options: $$2$$, target?: $$0$$): $$0$$ {
//   if (!target) 
//   target = this.create();
//   target.value = this.refJsonReader.scalar(json, 9, undefined, "value") as string;
//   return target;
//   }

// google.protobuf.DoubleValue (S,T:JsonWriteOptions,T:JsonReadOptions,T:JsonValue)
//
//   /**
//   * Encode `DoubleValue` to JSON number. 
//   */
//   function internalJsonWrite(message: $$0$$, options: $$1$$): $$3$$ {
//   return this.refJsonWriter.scalar(2, message.value, "value", false, true);
//   }
//
//   /**
//   * Decode `DoubleValue` from JSON number.
//   */
//   function internalJsonRead(json: $$3$$, options: $$2$$, target?: $$0$$): $$0$$ {
//   if (!target) 
//   target = this.create();
//   target.value = this.refJsonReader.scalar(json, 1, undefined, "value") as number;
//   return target;
//   }

// google.protobuf.FloatValue (S,T:JsonWriteOptions,T:JsonReadOptions,T:JsonValue)
//
//   /**
//   * Encode `FloatValue` to JSON number. 
//   */
//   function internalJsonWrite(message: $$0$$, options: $$1$$): $$3$$ {
//   return this.refJsonWriter.scalar(1, message.value, "value", false, true);
//   }
//
//   /**
//   * Decode `FloatValue` from JSON number.
//   */
//   function internalJsonRead(json: $$3$$, options: $$2$$, target?: $$0$$): $$0$$ {
//   if (!target) 
//   target = this.create();
//   target.value = this.refJsonReader.scalar(json, 1, undefined, "value") as number;
//   return target;
//   }

// google.protobuf.Int32Value (S,T:JsonWriteOptions,T:JsonReadOptions,T:JsonValue)
//
//   /**
//   * Encode `Int32Value` to JSON string. 
//   */
//   function internalJsonWrite(message: $$0$$, options: $$1$$): $$3$$ {
//   return this.refJsonWriter.scalar(5, message.value, "value", false, true);
//   }
//
//   /**
//   * Decode `Int32Value` from JSON string.
//   */
//   function internalJsonRead(json: $$3$$, options: $$2$$, target?: $$0$$): $$0$$ {
//   if (!target) 
//   target = this.create();
//   target.value = this.refJsonReader.scalar(json, 5, undefined, "value") as number;
//   return target;
//   }

// google.protobuf.UInt32Value (S,T:JsonWriteOptions,T:JsonReadOptions,T:JsonValue)
//
//   /**
//   * Encode `UInt32Value` to JSON string. 
//   */
//   function internalJsonWrite(message: $$0$$, options: $$1$$): $$3$$ {
//   return this.refJsonWriter.scalar(13, message.value, "value", false, true);
//   }
//
//   /**
//   * Decode `UInt32Value` from JSON string.
//   */
//   function internalJsonRead(json: $$3$$, options: $$2$$, target?: $$0$$): $$0$$ {
//   if (!target) 
//   target = this.create();
//   target.value = this.refJsonReader.scalar(json, 13, undefined, "value") as number;
//   return target;
//   }

// google.protobuf.Int64Value (S,N:LongType,T:JsonWriteOptions,T:JsonReadOptions,T:JsonValue,N:ScalarType,E)
//
//   /**
//   * Encode `Int64Value` to JSON string. 
//   */
//   function internalJsonWrite(message: $$0$$, options: $$2$$): $$4$$ {
//   return this.refJsonWriter.scalar($$5$$.INT64, message.value, "value", false, true);
//   }
//
//   /**
//   * Decode `Int64Value` from JSON string.
//   */
//   function internalJsonRead(json: $$4$$, options: $$3$$, target?: $$0$$): $$0$$ {
//   if (!target) 
//   target = this.create();
//   target.value = this.refJsonReader.scalar(json, $$5$$.INT64, $$1$$.$$6$$, "value") as any;
//   return target;
//   }

// google.protobuf.UInt64Value (S,N:LongType,T:JsonWriteOptions,T:JsonReadOptions,T:JsonValue,N:ScalarType,E)
//
//   /**
//   * Encode `UInt64Value` to JSON string. 
//   */
//   function internalJsonWrite(message: $$0$$, options: $$2$$): $$4$$ {
//   return this.refJsonWriter.scalar($$5$$.UINT64, message.value, "value", false, true);
//   }
//
//   /**
//   * Decode `UInt64Value` from JSON string.
//   */
//   function internalJsonRead(json: $$4$$, options: $$3$$, target?: $$0$$): $$0$$ {
//   if (!target) 
//   target = this.create();
//   target.value = this.refJsonReader.scalar(json, $$5$$.UINT64, $$1$$.$$6$$, "value") as any;
//   return target;
//   }

// google.protobuf.BytesValue (S,T:JsonWriteOptions,T:JsonReadOptions,T:JsonValue)
//
//   /**
//   * Encode `BytesValue` to JSON string. 
//   */
//   function internalJsonWrite(message: $$0$$, options: $$1$$): $$3$$ {
//   return this.refJsonWriter.scalar(12, message.value, "value", false, true);
//   }
//
//   /**
//   * Decode `BytesValue` from JSON string.
//   */
//   function internalJsonRead(json: $$3$$, options: $$2$$, target?: $$0$$): $$0$$ {
//   if (!target) 
//   target = this.create();
//   target.value = this.refJsonReader.scalar(json, 12, undefined, "value") as Uint8Array;
//   return target;
//   }

// google.type.Color (S)
//
//   /**
//   * Returns hexadecimal notation of the color: #RRGGBB[AA]
//   *
//   * R (red), G (green), B (blue), and A (alpha) are hexadecimal characters
//   * (0–9, A–F). A is optional. For example, #ff0000 is equivalent to
//   * #ff0000ff.
//   *
//   * See https://developer.mozilla.org/en-US/docs/Web/CSS/color_value#RGB_colors
//   */
//   function toHex(message: $$0$$): string {
//   let hex = [
//   message.red.toString(16),
//   message.green.toString(16),
//   message.blue.toString(16),
//   ];
//   if (message.alpha) {
//   let alpha = Math.max(Math.min(message.alpha.value, 1), 0);
//   hex.push(Math.round(alpha * 255).toString(16));
//   }
//   return '#' + hex.map(i => i.length < 2 ? '0' + i : i).join('');
//   }
//
//   /**
//   * Parses a hexadecimal color notation.
//   *
//   * Recognizes the following forms:
//   * - three-digit  (#RGB)
//   * - six-digit (#RRGGBB)
//   * - four-digit  (#RGBA)
//   * - eight-digit (#RRGGBBAA)
//   */
//   function fromHex(hex: string): $$0$$ {
//   if (/^#(?:[0-9a-fA-F]{3}){1}$/.test(hex)) {
//   // #RGB
//   return {
//   red: parseInt(hex.substring(1, 2) + hex.substring(1, 2), 16),
//   green: parseInt(hex.substring(2, 3) + hex.substring(2, 3), 16),
//   blue: parseInt(hex.substring(3, 4) + hex.substring(3, 4), 16),
//   };
//   } else if (/^#(?:[0-9a-fA-F]{3}){2}$/.test(hex)) {
//   // #RRGGBB
//   return {
//   red: parseInt(hex.substring(1, 3), 16),
//   green: parseInt(hex.substring(3, 5), 16),
//   blue: parseInt(hex.substring(5, 7), 16),
//   };
//   } else if (/^#(?:[0-9a-fA-F]{4}){1}$/.test(hex)) {
//   // #RGBA
//   return {
//   red: parseInt(hex.substring(1, 2) + hex.substring(1, 2), 16),
//   green: parseInt(hex.substring(2, 3) + hex.substring(2, 3), 16),
//   blue: parseInt(hex.substring(3, 4) + hex.substring(3, 4), 16),
//   alpha: {
//   value: parseInt(hex.substring(4, 5) + hex.substring(4, 5), 16) / 255,
//   }
//   };
//   } else if (/^#(?:[0-9a-fA-F]{4}){2}$/.test(hex)) {
//   // #RRGGBBAA
//   return {
//   red: parseInt(hex.substring(1, 3), 16),
//   green: parseInt(hex.substring(3, 5), 16),
//   blue: parseInt(hex.substring(5, 7), 16),
//   alpha: {
//   value: parseInt(hex.substring(7, 9), 16) / 255,
//   }
//   };
//   }
//   throw new Error('invalid hex color');
//   }

// google.type.Date (S)
//
//   /**
//   * Creates a javascript Date object from the message.
//   *
//   * If you do not provide the optional parameters for time,
//   * the current time is used.
//   */
//   function toJsDate(message: $$0$$, hours?: number, minutes?: number, seconds?: number, ms?: number): globalThis.Date {
//   let now = new globalThis.Date();
//   return new globalThis.Date(
//   message.year,
//   message.month - 1,
//   message.day,
//   hours ?? now.getHours(),
//   minutes ?? now.getMinutes(),
//   seconds ?? now.getSeconds(),
//   ms ?? now.getMilliseconds(),
//   );
//   }
//
//   /**
//   * Creates a Date message from a javascript Date object. 
//   */
//   function fromJsDate(date: globalThis.Date): $$0$$ {
//   return {
//   year: date.getFullYear(),
//   month: date.getMonth() + 1,
//   day: date.getDate(),
//   };
//   }

// google.type.DateTime (S,N:PbLong,L:utc_offset,L:time_offset,L:time_zone,C)
//
//   /**
//   * Creates `DateTime` for the current time.
//   */
//   function now(): $$0$$ {
//   return this.fromJsDate(new globalThis.Date());
//   }
//
//   /**
//   * Creates a javascript Date object from the message.
//   *
//   * If a the message has a UTC offset, the javascript Date is converted
//   * into your local time zone, because javascript Dates are always in the
//   * local time zone.
//   *
//   * If the message has an offset given as an IANA timezone id, an error is
//   * thrown, because javascript has no on-board support for IANA time zone
//   * ids.
//   */
//   function toJsDate(message: $$0$$): globalThis.Date {
//   let dt = new globalThis.Date(
//   message.year,
//   message.month - 1,
//   message.day,
//   message.hours,
//   message.minutes,
//   message.seconds,
//   message.nanos / 1000,
//   ),
//   to = message.$$3$$;
//   if (to) {
//   if (to.oneofKind === "$$4$$")
//   throw new globalThis.Error("IANA time zone not supported");
//   if (to.oneofKind === "$$2$$") {
//   let s = $$1$$.from(to.$$2$$.seconds).toNumber();
//   dt = new globalThis.Date(dt.getTime() - (s * 1000));
//   }
//   }
//   return dt;
//   }
//
//   /**
//   * Creates a Date message from a javascript Date object.
//   *  
//   * Values are in local time and a proper UTF offset is provided.
//   */
//   function fromJsDate(date: globalThis.Date): $$0$$ {
//   return {
//   year: date.getFullYear(),
//   month: date.getMonth() + 1,
//   day: date.getDate(),
//   hours: date.getHours(),
//   minutes: date.getMinutes(),
//   seconds: date.getSeconds(),
//   nanos: date.getMilliseconds() * 1000,
//   $$3$$: {
//   oneofKind: "$$2$$",
//   $$2$$: {
//   seconds: $$1$$.from(date.getTimezoneOffset() * 60).$$5$$(),
//   nanos: 0,
//   }
//   }
//   };
//   }

// google.type.TimeOfDay (S)
//
//   /**
//   * Creates a TimeOfDay message from a javascript Date object.
//   */
//   function fromJsDate(date: globalThis.Date): $$0$$ {
//   return {
//   hours: date.getHours(),
//   minutes: date.getMinutes(),
//   seconds: date.getSeconds(),
//   nanos: date.getMilliseconds() * 1000,
//   };
//   }

static const char *const snippet_strings[] = {
	"target",
	"message",
	"json",
	"$$0$$",
	"value",
	"options",
	"substring",
	"Error",
	"hex",
	"$$3$$",
	"globalThis",
	"$$1$$",
	"type",
	"$$2$$",
	"0",
	"Date",
	"nanos",
	"date",
	"create",
	"ms",
	"nanosStr",
	"1",
	"16",
	"internalJsonWrite",
	"3",
	"typeName",
	"internalJsonRead",
	"$$4$$",
	"parseInt",
	"scalar",
	"seconds",
	"kind",
	"$$9$$",
	"any",
	"$$6$$",
	"$$5$$",
	"1000",
	"matches",
	"2",
	"oneofKind",
	"undefined",
	"5",
	"T",
	"from",
	"refJsonReader",
	".",
	"length",
	"now",
	"msg",
	"v",
	"string",
	"name",
	"url",
	"Math",
	"toString",
	"4",
	"s",
	"refJsonWriter",
	"$$8$$",
	"@type",
	"fromJson",
	"toNumber",
	"6",
	"7",
	"alpha",
	"$$7$$",
	"parse",
	"z",
	"Z",
	"match",
	"9",
	"text",
	"map",
	"test",
	"red",
	"green",
	"blue",
	"hours",
	"minutes",
	"to",
	"typeUrlToName",
	"",
	"find",
	"t",
	"315576000000",
	"longSeconds",
	"p",
	"fields",
	"k",
	"toJson",
	"Value",
	"numberValue",
	"f",
	"$$10$$",
	"values",
	"i",
	"year",
	"month",
	"day",
	"fromJsDate",
	"dt",
	"opt",
	"copy",
	"Object",
	"slash",
	"1000000",
	"PbLong",
	"-",
	"8",
	"repeat",
	"sign",
	"str",
	" from JSON ",
	"listValueField",
	"structValueField",
	"255",
	"getHours",
	"getMinutes",
	"getSeconds",
	"getMilliseconds",
	"typeNameToUrl",
	"toBinary",
	"contains",
	"fromBinary",
	"wants",
	"has",
	"typeRegistry",
	" is not available in the type registry.",
	"startsWith",
	"google.protobuf.",
	"invalid type url: ",
	"floor",
	"getTime",
	"In JSON format, the `Timestamp` type is encoded as a string \n in the RFC 3339 format.",
	"0001-01-01T00:00:00Z",
	"9999-12-31T23:59:59Z",
	"1000000000",
	"000000",
	"000",
	"replace",
	":",
	"Number",
	". Expected string.",
	"secs",
	"invalidFieldMaskJsonRegex",
	"join",
	"paths",
	",",
	"camelToSnake",
	"_",
	"sc",
	"letter",
	"entries",
	"number",
	"no",
	"isArray",
	"Array",
	"Unable to parse ",
	"push",
	"13",
	"INT64",
	"UINT64",
	"12",
	"toJsDate",
	"getFullYear",
	"getMonth",
	"getDate",
	"Pack the message into a new `Any`.\n\n Uses 'type.googleapis.com/full.type.name' as the type URL.",
	"pack",
	"Unpack the message from the `Any`.",
	"unpack",
	"Cannot unpack google.protobuf.Any with $$9$$ '",
	"' as ",
	"Partial",
	"Does the given `Any` contain a packed message of the given type\?",
	"Convert the message to canonical JSON value.\n\n You have to provide the `typeRegistry` option so that the\n packed message can be converted to JSON.\n\n The `typeRegistry` option is also required to read\n `google.protobuf.Any` from JSON format.",
	"Unable to convert google.protobuf.Any with typeUrl '",
	"' to JSON. The specified type ",
	"readUnknownField",
	"Unable to parse google.protobuf.Any from JSON ",
	"Unable to parse google.protobuf.Any from JSON. The specified type ",
	"hasOwnProperty",
	"assign",
	"invalid type name: ",
	"type.googleapis.com/",
	"lastIndexOf",
	"/",
	"Creates a new `Timestamp` for the current time.",
	"Converts a `Timestamp` to a JavaScript Date.",
	"toDate",
	"ceil",
	"Converts a JavaScript Date to a `Timestamp`.",
	"fromDate",
	"Unable to encode Timestamp to JSON. Must be from 0001-01-01T00:00:00Z to 9999-12-31T23:59:59Z inclusive.",
	"Unable to encode invalid Timestamp to JSON. Nanos must not be negative.",
	"toISOString",
	".000Z",
	"Unable to parse Timestamp from JSON ",
	"/^([0-9]{4})-([0-9]{2})-([0-9]{2})T([0-9]{2}):([0-9]{2}):([0-9]{2})(\?:Z|\\.([0-9]{3,9})Z|([+-][0-9][0-9]:[0-9][0-9]))$/",
	"Unable to parse Timestamp from JSON. Invalid format.",
	"isNaN",
	"Unable to parse Timestamp from JSON. Invalid value.",
	"Unable to parse Timestamp from JSON. Must be from 0001-01-01T00:00:00Z to 9999-12-31T23:59:59Z inclusive.",
	"Encode `Duration` to JSON string like \"3.000001s\".",
	"Duration value out of range.",
	"abs",
	"Decode `Duration` from JSON string like \"3.000001s\"",
	"Unable to parse Duration from JSON ",
	"/^(-\?)([0-9]+)(\?:\\.([0-9]+))\?s/",
	"Unable to parse Duration from JSON string. Invalid format.",
	"Unable to parse Duration from JSON string. Value out of range.",
	"Encode `FieldMask` to JSON object.",
	"/[A-Z]|(_([.0-9_]|$))/g",
	"Unable to encode FieldMask to JSON. lowerCamelCase of path name \"",
	"\" is irreversible.",
	"Decode `FieldMask` from JSON object.",
	"Unable to parse FieldMask from JSON ",
	"includes",
	"Unable to parse FieldMask from JSON. Path names must be lowerCamelCase.",
	"/[A-Z]/g",
	"toLowerCase",
	"split",
	"Encode `Struct` to JSON object.",
	"Decode `Struct` from JSON object.",
	"Unable to parse message ",
	"Encode `Value` to JSON value.",
	"isFinite",
	"Decode `Value` from JSON value.",
	"boolean",
	"object",
	"NULL_VALUE",
	"NullValue",
	"ListValue",
	"Struct",
	"Encode `ListValue` to JSON array.",
	"Decode `ListValue` from JSON array.",
	"Encode `BoolValue` to JSON bool.",
	"Decode `BoolValue` from JSON bool.",
	"Encode `StringValue` to JSON string.",
	"Decode `StringValue` from JSON string.",
	"Encode `DoubleValue` to JSON number.",
	"Decode `DoubleValue` from JSON number.",
	"Encode `FloatValue` to JSON number.",
	"Decode `FloatValue` from JSON number.",
	"Encode `Int32Value` to JSON string.",
	"Decode `Int32Value` from JSON string.",
	"Encode `UInt32Value` to JSON string.",
	"Decode `UInt32Value` from JSON string.",
	"Encode `Int64Value` to JSON string.",
	"Decode `Int64Value` from JSON string.",
	"Encode `UInt64Value` to JSON string.",
	"Decode `UInt64Value` from JSON string.",
	"Encode `BytesValue` to JSON string.",
	"Decode `BytesValue` from JSON string.",
	"Uint8Array",
	"Returns hexadecimal notation of the color: #RRGGBB[AA]\n\n R (red), G (green), B (blue), and A (alpha) are hexadecimal characters\n (0\342\200\2239, A\342\200\223F). A is optional. For example, #ff0000 is equivalent to\n #ff0000ff.\n\n See https://developer.mozilla.org/en-US/docs/Web/CSS/color_value#RGB_colors",
	"toHex",
	"max",
	"min",
	"round",
	"#",
	"Parses a hexadecimal color notation.\n\n Recognizes the following forms:\n - three-digit  (#RGB)\n - six-digit (#RRGGBB)\n - four-digit  (#RGBA)\n - eight-digit (#RRGGBBAA)",
	"fromHex",
	"/^#(\?:[0-9a-fA-F]{3}){1}$/",
	"/^#(\?:[0-9a-fA-F]{3}){2}$/",
	"/^#(\?:[0-9a-fA-F]{4}){1}$/",
	"/^#(\?:[0-9a-fA-F]{4}){2}$/",
	"invalid hex color",
	"Creates a javascript Date object from the message.\n\n If you do not provide the optional parameters for time,\n the current time is used.",
	"Creates a Date message from a javascript Date object.",
	"Creates `DateTime` for the current time.",
	"Creates a javascript Date object from the message.\n\n If a the message has a UTC offset, the javascript Date is converted\n into your local time zone, because javascript Dates are always in the\n local time zone.\n\n If the message has an offset given as an IANA timezone id, an error is\n thrown, because javascript has no on-board support for IANA time zone\n ids.",
	"IANA time zone not supported",
	"Creates a Date message from a javascript Date object.\n  \n Values are in local time and a proper UTF offset is provided.",
	"getTimezoneOffset",
	"60",
	"Creates a TimeOfDay message from a javascript Date object.",
};

static const uint8_t snippet_code[] = {
	189,9,167,1,0,208,7,1,173,2,168,1,0,4,164,5,35,173,2,3,0,0,6,254,
	6,8,48,2,172,7,3,134,6,8,50,5,228,8,1,173,2,32,0,5,144,6,3,136,
	6,1,173,2,120,0,3,160,3,0,0,46,2,136,6,1,173,2,25,0,3,173,2,12,
	0,0,0,0,228,8,1,173,2,4,0,5,144,6,3,136,6,1,173,2,121,0,3,173,
	2,12,0,0,46,2,173,2,1,0,0,0,0,0,0,43,2,236,4,1,173,2,42,0,
	30,180,4,0,0,44,4,240,4,1,173,2,1,0,4,164,5,35,173,2,42,0,0,0,
	240,4,1,173,2,12,0,4,164,5,35,173,2,11,0,45,2,1,26,0,0,0,189,9,
	169,1,0,208,7,1,173,2,170,1,0,4,1,45,6,254,6,8,48,4,140,7,3,190,
	6,64,53,26,144,6,3,136,6,1,173,2,122,0,3,160,3,0,0,46,4,173,2,33,
	0,173,2,12,0,0,0,21,188,7,3,148,6,3,173,2,7,0,46,2,196,6,7,196,
	6,7,196,6,7,196,6,7,41,171,1,0,8,136,6,1,173,2,32,0,3,173,2,33,
	0,0,9,156,1,0,0,8,41,172,1,0,9,156,1,0,0,8,1,221,1,9,156,1,
	0,0,8,41,45,0,9,156,1,0,0,0,0,0,172,7,3,144,6,3,136,6,1,173,
	2,123,0,3,173,2,12,0,0,46,4,136,6,1,173,2,4,0,3,173,2,33,0,0,
	173,2,5,0,0,0,0,43,2,1,232,1,44,6,240,4,1,173,2,33,0,4,1,216,
	2,0,1,218,1,240,4,1,173,2,5,0,4,164,5,35,173,2,173,1,0,45,2,164,
	5,35,173,2,13,0,0,0,10,228,1,0,0,0,189,9,174,1,0,208,7,1,173,2,
	122,0,4,128,4,0,6,254,6,8,48,8,140,7,3,190,6,64,53,26,136,6,1,173,
	2,46,0,3,1,183,1,0,0,21,172,7,3,236,2,0,0,0,132,7,40,206,7,16,
	1,51,2,200,7,1,173,2,124,0,5,200,6,10,228,1,0,11,232,1,0,18,196,6,
	7,176,6,3,173,2,12,0,0,8,41,50,0,9,136,1,0,0,19,173,2,12,0,20,
	1,187,3,0,0,0,0,132,7,40,206,7,16,1,51,2,200,7,1,173,2,125,0,5,
	144,6,3,136,6,1,173,2,80,0,3,160,3,0,0,46,2,1,164,2,0,0,0,0,
	172,7,3,196,6,7,173,2,124,0,8,173,2,125,0,9,144,1,0,0,0,0,44,4,
	1,226,1,240,4,1,173,2,12,0,4,200,5,52,4,164,5,35,173,2,11,0,45,2,
	244,3,0,0,188,4,0,0,0,0,189,9,175,1,0,208,7,1,173,2,23,0,4,164,
	5,35,173,2,34,0,0,6,254,6,8,48,20,140,7,3,196,6,7,1,134,3,8,41,
	81,0,9,144,1,0,0,21,172,7,3,132,6,50,0,0,0,0,132,7,40,206,7,16,
	1,51,2,200,7,1,173,2,25,0,5,1,155,1,0,0,0,132,7,40,206,7,16,1,
	51,2,200,7,1,173,2,101,0,5,144,6,3,173,2,35,0,46,2,173,2,5,0,0,
	0,0,0,132,7,40,206,7,16,1,51,2,200,7,1,173,2,12,0,5,144,6,3,138,
	6,16,32,1,173,2,82,0,3,136,6,1,173,2,126,0,3,173,2,101,0,0,15,112,
	0,0,46,2,170,6,128,2,6,196,6,7,136,6,1,173,2,25,0,3,173,2,83,0,
	0,8,173,2,25,0,9,144,1,0,0,16,152,1,0,44,2,240,4,1,173,2,83,0,
	0,0,0,0,0,0,140,7,3,190,6,64,53,26,173,2,12,0,0,21,188,7,3,148,
	6,3,136,6,1,173,2,7,0,3,173,2,10,0,0,46,2,196,6,7,196,6,7,196,
	6,7,196,6,7,41,176,1,0,8,1,237,4,9,156,1,0,0,8,41,177,1,0,9,
	156,1,0,0,8,173,2,25,0,9,156,1,0,0,8,41,127,0,9,156,1,0,0,0,
	0,0,132,7,40,206,7,16,1,51,2,200,7,1,173,2,4,0,5,144,6,3,1,242,
	4,46,4,1,232,4,132,6,50,2,228,8,1,173,2,178,1,0,5,236,2,0,0,0,
	0,0,0,0,132,7,40,206,7,16,1,51,2,200,7,1,173,2,2,0,5,144,6,3,
	136,6,1,173,2,23,0,3,173,2,12,0,0,46,4,173,2,4,0,173,2,101,0,0,
	0,0,0,140,7,3,196,6,7,144,6,3,136,6,1,173,2,128,1,0,3,173,2,25,
	0,0,46,2,41,129,1,0,0,8,190,6,64,53,26,144,6,3,173,2,58,0,46,2,
	173,2,2,0,0,0,9,224,1,0,0,21,136,7,3,196,6,7,173,2,2,0,8,132,
	6,50,2,228,8,1,173,2,4,0,5,173,2,2,0,0,0,9,248,1,0,0,0,0,
	136,7,3,196,6,7,140,6,3,173,2,2,0,25,41,59,0,0,8,1,230,6,9,248,
	1,0,0,0,172,7,3,173,2,2,0,0,0,44,4,1,153,6,240,4,1,173,2,5,
	0,4,164,5,35,173,2,9,0,0,0,0,208,7,1,173,2,26,0,4,1,134,9,6,
	254,6,8,48,22,140,7,3,1,142,1,21,188,7,3,148,6,3,1,224,2,46,2,196,
	6,7,196,6,7,41,179,1,0,8,144,6,3,173,2,65,0,46,2,173,2,2,0,0,
	9,156,1,0,0,8,41,45,0,9,156,1,0,0,0,0,0,140,7,3,196,6,7,196,
	6,7,176,6,3,1,143,1,0,8,41,50,0,9,140,1,0,0,8,196,6,7,1,160,
	1,8,41,81,0,9,136,1,0,0,9,224,1,0,0,21,172,7,3,144,6,3,136,6,
	1,173,2,18,0,3,160,3,0,0,46,0,0,0,0,132,7,40,206,7,16,1,51,2,
	200,7,1,173,2,25,0,5,144,6,3,1,160,6,46,2,1,226,1,0,0,0,0,132,
	7,40,206,7,16,1,51,2,200,7,1,173,2,12,0,5,144,6,3,138,6,16,32,1,
	173,2,82,0,3,138,6,16,32,1,173,2,126,0,3,173,2,5,0,15,112,0,0,15,
	112,0,0,46,2,170,6,128,2,6,196,6,7,1,241,4,8,173,2,25,0,9,136,1,
	0,0,16,152,1,0,44,2,1,231,4,0,0,0,0,0,140,7,3,1,226,4,21,188,
	7,3,148,6,3,1,219,4,46,2,196,6,7,196,6,7,41,180,1,0,8,173,2,25,
	0,9,156,1,0,0,8,41,127,0,9,156,1,0,0,0,0,0,132,7,40,206,7,16,
	1,51,2,200,7,1,173,2,4,0,0,0,0,140,7,3,196,6,7,1,234,3,8,144,
	6,3,136,6,1,173,2,181,1,0,3,173,2,2,0,0,46,2,41,4,0,0,9,220,
	1,0,0,21,136,7,3,196,6,7,173,2,4,0,8,144,6,3,136,6,1,173,2,60,
	0,3,173,2,12,0,0,46,4,140,6,3,173,2,2,0,25,41,4,0,0,173,2,5,
	0,0,9,248,1,0,0,0,22,254,6,8,48,6,132,7,40,206,7,16,1,51,2,200,
	7,1,173,2,102,0,5,144,6,3,136,6,1,173,2,182,1,0,3,173,2,103,0,0,
	46,4,1,207,7,173,2,2,0,0,0,0,0,136,7,3,172,6,3,140,6,3,173,2,
	102,0,25,41,59,0,0,0,0,136,7,3,196,6,7,173,2,4,0,8,144,6,3,1,
	125,46,4,173,2,102,0,173,2,5,0,0,9,248,1,0,0,0,0,0,140,7,3,196,
	6,7,173,2,0,0,8,173,2,40,0,9,144,1,0,0,21,136,7,3,196,6,7,173,
	2,0,0,8,1,185,3,9,248,1,0,0,0,0,136,7,3,196,6,7,136,6,1,173,
	2,32,0,3,173,2,0,0,0,8,1,140,5,9,248,1,0,0,0,136,7,3,196,6,
	7,136,6,1,173,2,4,0,3,173,2,0,0,0,8,144,6,3,1,171,13,46,2,173,
	2,4,0,0,9,248,1,0,0,0,172,7,3,173,2,0,0,0,0,44,6,240,4,1,
	173,2,2,0,4,1,182,9,0,240,4,1,173,2,5,0,4,164,5,35,173,2,27,0,
	0,0,240,4,1,173,2,0,0,4,1,180,14,10,228,1,0,0,0,208,7,1,173,2,
	120,0,4,188,4,0,6,254,6,8,48,4,140,7,3,190,6,64,53,26,136,6,1,173,
	2,46,0,3,173,2,51,0,0,0,21,188,7,3,148,6,3,173,2,7,0,46,2,196,
	6,7,41,183,1,0,8,173,2,51,0,9,156,1,0,0,0,0,0,172,7,3,196,6,
	7,41,184,1,0,8,173,2,51,0,9,156,1,0,0,0,0,44,2,240,4,1,173,2,
	51,0,4,188,4,0,0,0,208,7,1,173,2,80,0,4,188,4,0,6,254,6,8,48,
	10,140,7,3,190,6,64,53,26,136,6,1,173,2,46,0,3,173,2,52,0,0,0,21,
	188,7,3,148,6,3,173,2,7,0,46,2,196,6,7,41,130,1,0,8,173,2,52,0,
	9,156,1,0,0,0,0,0,132,7,40,206,7,16,1,51,2,200,7,1,173,2,104,0,
	5,144,6,3,136,6,1,173,2,185,1,0,3,173,2,52,0,0,46,2,41,186,1,0,
	0,0,0,0,132,7,40,206,7,16,1,51,2,200,7,1,173,2,51,0,5,200,6,10,
	228,1,0,11,232,1,0,18,196,6,7,173,2,104,0,8,33,14,0,9,124,0,0,19,
	144,6,3,136,6,1,173,2,6,0,3,173,2,52,0,0,46,2,196,6,7,173,2,104,
	0,8,33,21,0,9,156,1,0,0,0,20,173,2,52,0,0,0,0,0,140,7,3,1,
	168,2,21,1,171,1,0,172,7,3,173,2,51,0,0,0,44,2,240,4,1,173,2,52,
	0,4,188,4,0,0,0,189,9,187,1,0,208,7,1,173,2,47,0,4,1,166,17,6,
	254,6,8,48,10,132,7,40,206,7,16,2,51,2,200,7,1,173,2,48,0,5,1,195,
	7,0,0,0,132,7,40,206,7,16,2,51,2,200,7,1,173,2,19,0,5,144,6,3,
	136,6,1,173,2,47,0,3,173,2,15,0,0,46,0,0,0,0,0,136,7,3,196,6,
	7,136,6,1,173,2,30,0,3,173,2,48,0,0,8,144,6,3,136,6,1,173,2,34,
	0,3,144,6,3,136,6,1,173,2,43,0,3,173,2,11,0,0,46,2,144,6,3,136,
	6,1,173,2,131,1,0,3,173,2,53,0,0,46,2,196,6,7,173,2,19,0,8,33,
	36,0,9,172,1,0,0,0,0,0,46,0,0,9,248,1,0,0,0,136,7,3,196,6,
	7,136,6,1,173,2,16,0,3,173,2,48,0,0,8,196,6,7,160,6,3,196,6,7,
	173,2,19,0,8,33,36,0,9,176,1,0,0,0,8,33,105,0,9,164,1,0,0,9,
	248,1,0,0,0,172,7,3,173,2,48,0,0,0,44,0,0,189,9,188,1,0,208,7,
	1,173,2,189,1,0,4,164,5,35,173,2,15,0,0,6,254,6,8,48,2,172,7,3,
	148,6,3,173,2,15,0,46,2,196,6,7,196,6,7,144,6,3,136,6,1,173,2,61,
	0,3,144,6,3,1,192,1,46,2,136,6,1,173,2,30,0,3,173,2,1,0,0,0,
	0,46,0,0,8,33,36,0,9,164,1,0,0,8,144,6,3,136,6,1,173,2,190,1,
	0,3,173,2,53,0,0,46,2,196,6,7,136,6,1,173,2,16,0,3,173,2,1,0,
	0,8,33,105,0,9,172,1,0,0,0,9,156,1,0,0,0,0,0,44,2,240,4,1,
	173,2,1,0,4,1,175,20,0,0,189,9,191,1,0,208,7,1,173,2,192,1,0,4,
	1,194,20,6,254,6,8,48,10,1,156,3,132,7,40,206,7,16,2,51,2,200,7,1,
	173,2,19,0,5,144,6,3,136,6,1,173,2,132,1,0,3,173,2,17,0,0,46,0,
	0,0,0,0,136,7,3,196,6,7,1,137,3,8,144,6,3,136,6,1,173,2,34,0,
	3,144,6,3,136,6,1,173,2,43,0,3,173,2,106,0,0,46,2,1,255,2,0,0,
	46,0,0,9,248,1,0,0,0,136,7,3,196,6,7,1,222,2,8,196,6,7,160,6,
	3,196,6,7,1,218,2,8,160,6,3,200,6,10,228,1,0,11,232,1,0,18,196,6,
	7,196,6,7,173,2,19,0,8,33,14,0,9,116,0,0,8,196,6,7,1,255,2,8,
	33,14,0,9,148,1,0,0,9,220,1,0,0,19,33,36,0,20,33,14,0,0,0,9,
	156,1,0,0,0,8,33,105,0,9,164,1,0,0,9,248,1,0,0,0,1,143,3,0,
	44,2,240,4,1,173,2,17,0,4,1,131,3,0,0,189,9,133,1,0,208,7,1,173,
	2,23,0,4,1,147,8,6,254,6,8,48,12,132,7,40,206,7,16,1,51,2,200,7,
	1,173,2,19,0,5,196,6,7,144,6,3,136,6,1,173,2,61,0,3,144,6,3,1,
	211,1,46,2,1,146,3,0,0,46,0,0,8,33,36,0,9,164,1,0,0,0,0,0,
	140,7,3,196,6,7,196,6,7,173,2,19,0,8,144,6,3,136,6,1,173,2,66,0,
	3,173,2,15,0,0,46,2,41,134,1,0,0,9,116,0,0,8,196,6,7,173,2,19,
	0,8,144,6,3,1,36,46,2,41,135,1,0,0,9,124,0,0,9,224,1,0,0,21,
	188,7,3,148,6,3,173,2,7,0,46,2,41,193,1,0,0,0,0,140,7,3,196,6,
	7,1,213,3,8,33,14,0,9,116,0,0,21,188,7,3,148,6,3,173,2,7,0,46,
	2,41,194,1,0,0,0,0,132,7,40,206,7,16,1,51,2,200,7,1,173,2,67,0,
	5,41,68,0,0,0,0,140,7,3,196,6,7,1,145,4,8,33,14,0,9,124,0,0,
	21,254,6,8,48,4,132,7,40,206,7,16,1,51,2,200,7,1,173,2,20,0,5,144,
	6,3,136,6,1,173,2,6,0,3,144,6,3,136,6,1,173,2,54,0,3,160,6,3,
	196,6,7,1,207,4,8,33,136,1,0,9,156,1,0,0,0,0,46,0,0,0,46,2,
	33,21,0,0,0,0,0,140,7,3,196,6,7,144,6,3,136,6,1,173,2,6,0,3,
	173,2,20,0,0,46,2,33,24,0,0,8,41,137,1,0,9,144,1,0,0,21,136,7,
	3,196,6,7,173,2,67,0,8,196,6,7,196,6,7,41,45,0,8,144,6,3,1,54,
	46,4,33,14,0,33,24,0,0,9,156,1,0,0,8,41,68,0,9,156,1,0,0,9,
	248,1,0,0,0,22,140,7,3,196,6,7,144,6,3,1,95,46,2,33,62,0,0,8,
	41,138,1,0,9,144,1,0,0,21,136,7,3,196,6,7,173,2,67,0,8,196,6,7,
	196,6,7,41,45,0,8,144,6,3,1,138,1,46,4,33,14,0,33,62,0,0,9,156,
	1,0,0,8,41,68,0,9,156,1,0,0,9,248,1,0,0,0,22,136,7,3,196,6,
	7,173,2,67,0,8,196,6,7,196,6,7,41,45,0,8,173,2,20,0,9,156,1,0,
	0,8,41,68,0,9,156,1,0,0,9,248,1,0,0,0,0,0,0,0,172,7,3,144,
	6,3,136,6,1,173,2,139,1,0,3,144,6,3,136,6,1,173,2,195,1,0,3,148,
	6,3,173,2,15,0,46,2,173,2,19,0,0,0,46,0,0,0,46,4,41,196,1,0,
	173,2,67,0,0,0,0,44,4,1,236,6,240,4,1,173,2,5,0,4,1,173,24,0,
	0,189,9,133,1,0,208,7,1,173,2,26,0,4,1,176,27,6,254,6,8,48,22,140,
	7,3,196,6,7,176,6,3,173,2,2,0,0,8,41,50,0,9,148,1,0,0,21,188,
	7,3,148,6,3,173,2,7,0,46,2,196,6,7,196,6,7,41,197,1,0,8,144,6,
	3,173,2,35,0,46,2,173,2,2,0,0,9,156,1,0,0,8,41,45,0,9,156,1,
	0,0,0,0,0,132,7,40,206,7,16,1,51,2,200,7,1,173,2,37,0,5,144,6,
	3,136,6,1,173,2,69,0,3,173,2,2,0,0,46,2,53,198,1,0,0,0,0,0,
	140,7,3,190,6,64,53,26,173,2,37,0,0,21,188,7,3,148,6,3,173,2,7,0,
	46,2,41,199,1,0,0,0,0,132,7,40,206,7,16,1,51,2,200,7,1,173,2,19,
	0,5,144,6,3,1,220,5,46,2,196,6,7,196,6,7,196,6,7,196,6,7,196,6,
	7,196,6,7,196,6,7,196,6,7,196,6,7,196,6,7,196,6,7,140,6,3,173,2,
	37,0,25,33,21,0,0,8,41,107,0,9,156,1,0,0,8,140,6,3,173,2,37,0,
	25,33,38,0,0,9,156,1,0,0,8,41,107,0,9,156,1,0,0,8,140,6,3,173,
	2,37,0,25,33,24,0,0,9,156,1,0,0,8,41,42,0,9,156,1,0,0,8,140,
	6,3,173,2,37,0,25,33,55,0,0,9,156,1,0,0,8,41,140,1,0,9,156,1,
	0,0,8,140,6,3,173,2,37,0,25,33,41,0,0,9,156,1,0,0,8,41,140,1,
	0,9,156,1,0,0,8,140,6,3,173,2,37,0,25,33,62,0,0,9,156,1,0,0,
	8,160,6,3,200,6,10,228,1,0,11,232,1,0,18,140,6,3,173,2,37,0,25,33,
	108,0,0,19,1,13,20,41,68,0,0,0,9,156,1,0,0,0,0,0,0,140,7,3,
	144,6,3,136,6,1,173,2,200,1,0,3,173,2,141,1,0,0,46,2,173,2,19,0,
	0,21,188,7,3,148,6,3,173,2,7,0,46,2,41,201,1,0,0,0,0,140,7,3,
	1,133,8,21,188,7,3,148,6,3,1,208,24,46,2,41,202,1,0,0,0,0,140,7,
	3,190,6,64,53,26,173,2,0,0,0,21,1,251,17,0,136,7,3,196,6,7,136,6,
	1,173,2,30,0,3,173,2,0,0,0,8,144,6,3,136,6,1,173,2,34,0,3,144,
	6,3,1,190,10,46,2,1,160,13,0,0,46,0,0,9,248,1,0,0,0,136,7,3,
	196,6,7,136,6,1,173,2,16,0,3,173,2,0,0,0,8,33,14,0,9,248,1,0,
	0,0,140,7,3,140,6,3,173,2,37,0,25,33,63,0,0,21,136,7,3,196,6,7,
	1,45,8,160,6,3,196,6,7,144,6,3,173,2,28,0,46,2,196,6,7,196,6,7,
	41,21,0,8,1,47,9,156,1,0,0,8,144,6,3,136,6,1,173,2,109,0,3,41,
	14,0,0,46,2,196,6,7,33,70,0,8,136,6,1,173,2,46,0,3,1,87,0,9,
	160,1,0,0,0,9,156,1,0,0,0,8,33,136,1,0,9,160,1,0,0,0,9,248,
	1,0,0,0,0,1,243,18,0,44,6,240,4,1,173,2,2,0,4,1,226,18,0,1,
	157,24,1,224,18,0,189,9,203,1,0,208,7,1,173,2,23,0,4,1,250,18,6,254,
	6,8,48,12,132,7,40,206,7,16,1,51,2,200,7,1,173,2,56,0,5,1,246,13,
	0,0,0,140,7,3,196,6,7,196,6,7,173,2,56,0,8,33,84,0,9,124,0,0,
	8,196,6,7,173,2,56,0,8,190,6,64,40,26,33,84,0,0,9,116,0,0,9,224,
	1,0,0,21,188,7,3,148,6,3,173,2,7,0,46,2,41,204,1,0,0,0,0,132,
	7,40,206,7,16,1,51,2,200,7,1,173,2,71,0,5,144,6,3,136,6,1,173,2,
	54,0,3,1,201,14,0,46,0,0,0,0,0,140,7,3,196,6,7,196,6,7,173,2,
	56,0,8,33,14,0,9,144,1,0,0,8,1,230,10,9,220,1,0,0,21,136,7,3,
	196,6,7,173,2,71,0,8,196,6,7,41,107,0,8,173,2,71,0,9,156,1,0,0,
	9,248,1,0,0,0,0,140,7,3,196,6,7,1,233,14,8,33,14,0,9,148,1,0,
	0,21,254,6,8,48,8,132,7,40,206,7,16,1,51,2,200,7,1,173,2,20,0,5,
	144,6,3,136,6,1,173,2,54,0,3,144,6,3,136,6,1,173,2,205,1,0,3,173,
	2,53,0,0,46,2,1,170,15,0,0,46,0,0,0,0,0,136,7,3,196,6,7,173,
	2,20,0,8,196,6,7,144,6,3,1,147,3,46,2,196,6,7,33,70,0,8,136,6,
	1,173,2,46,0,3,173,2,20,0,0,9,160,1,0,0,0,8,173,2,20,0,9,156,
	1,0,0,9,248,1,0,0,0,140,7,3,1,138,11,21,136,7,3,196,6,7,173,2,
	20,0,8,1,224,10,9,248,1,0,0,0,22,140,7,3,1,199,10,21,136,7,3,196,
	6,7,173,2,20,0,8,1,168,10,9,248,1,0,0,0,0,0,136,7,3,196,6,7,
	173,2,71,0,8,1,140,10,9,252,1,0,0,0,0,0,172,7,3,196,6,7,173,2,
	71,0,8,41,56,0,9,156,1,0,0,0,0,44,4,1,186,16,1,206,9,0,189,9,
	206,1,0,208,7,1,173,2,26,0,4,1,245,36,6,254,6,8,48,20,140,7,3,1,
	197,9,21,188,7,3,148,6,3,173,2,7,0,46,2,196,6,7,196,6,7,41,207,1,
	0,8,1,180,9,9,156,1,0,0,8,41,142,1,0,9,156,1,0,0,0,0,0,132,
	7,40,206,7,16,1,51,2,200,7,1,173,2,69,0,5,144,6,3,1,170,9,46,2,
	53,208,1,0,0,0,0,0,140,7,3,196,6,7,173,2,69,0,8,144,3,0,9,144,
	1,0,0,21,188,7,3,148,6,3,173,2,7,0,46,2,41,209,1,0,0,0,0,1,
	225,6,132,7,40,206,7,16,1,51,2,200,7,1,248,5,49,8,220,6,0,252,5,1,
	173,2,110,0,0,252,5,1,173,2,143,1,0,0,252,5,1,173,2,16,0,0,0,5,
	173,2,69,0,0,0,0,132,7,40,206,7,16,1,51,2,200,7,1,173,2,85,0,5,
	144,6,3,1,182,20,46,2,196,6,7,173,2,110,0,8,1,56,9,156,1,0,0,0,
	0,0,0,140,7,3,196,6,7,196,6,7,144,6,3,136,6,1,173,2,61,0,3,173,
	2,85,0,0,46,0,0,8,33,84,0,9,124,0,0,8,196,6,7,1,31,8,1,173,
	5,9,116,0,0,9,224,1,0,0,21,188,7,3,148,6,3,173,2,7,0,46,2,41,
	210,1,0,0,0,0,136,7,3,196,6,7,1,254,7,8,144,6,3,136,6,1,173,2,
	34,0,3,173,2,85,0,0,46,0,0,9,248,1,0,0,0,140,7,3,196,6,7,176,
	6,3,173,2,16,0,0,8,41,50,0,9,136,1,0,0,21,254,6,8,48,4,132,7,
	40,206,7,16,1,51,2,200,7,1,173,2,20,0,5,196,6,7,196,6,7,173,2,110,
	0,8,173,2,16,0,9,156,1,0,0,8,144,6,3,1,216,7,46,2,196,6,7,33,
	70,0,8,136,6,1,173,2,46,0,3,173,2,16,0,0,9,160,1,0,0,0,9,156,
	1,0,0,0,0,0,136,7,3,196,6,7,1,217,8,8,144,6,3,173,2,28,0,46,
	2,173,2,20,0,0,9,248,1,0,0,0,0,0,1,212,26,0,44,6,1,225,7,1,
	245,31,1,184,26,0,189,9,211,1,0,208,7,1,173,2,23,0,4,1,210,26,6,254,
	6,8,48,4,132,7,40,206,7,16,2,51,2,200,7,1,173,2,144,1,0,5,53,212,
	1,0,0,0,0,172,7,3,144,6,3,136,6,1,173,2,145,1,0,3,144,6,3,136,
	6,1,173,2,72,0,3,136,6,1,173,2,146,1,0,3,173,2,1,0,0,0,46,2,
	170,6,128,2,6,254,6,8,48,4,140,7,3,144,6,3,136,6,1,173,2,73,0,3,
	1,80,0,46,2,173,2,86,0,0,21,188,7,3,148,6,3,173,2,7,0,46,2,196,
	6,7,196,6,7,41,213,1,0,8,173,2,86,0,9,156,1,0,0,8,41,214,1,0,
	9,156,1,0,0,0,0,0,172,7,3,144,6,3,173,2,9,0,46,2,173,2,86,0,
	0,0,0,16,152,1,0,44,2,240,4,1,173,2,86,0,0,0,0,0,46,2,41,147,
	1,0,0,0,0,44,4,1,154,22,240,4,1,173,2,5,0,4,164,5,35,173,2,11,
	0,0,0,0,189,9,215,1,0,208,7,1,173,2,26,0,4,1,227,42,6,254,6,8,
	48,12,140,7,3,1,179,15,21,188,7,3,148,6,3,173,2,7,0,46,2,196,6,7,
	196,6,7,41,216,1,0,8,1,162,15,9,156,1,0,0,8,41,142,1,0,9,156,1,
	0,0,0,0,0,1,135,12,140,7,3,196,6,7,173,2,2,0,8,41,81,0,9,144,
	1,0,0,21,1,178,29,0,132,7,40,206,7,16,1,51,2,200,7,1,173,2,148,1,
	0,5,168,6,6,254,6,8,48,6,140,7,3,144,6,3,136,6,1,173,2,217,1,0,
	3,173,2,111,0,0,46,2,41,149,1,0,0,21,188,7,3,148,6,3,173,2,7,0,
	46,2,41,218,1,0,0,0,0,132,7,40,206,7,16,1,51,2,200,7,1,173,2,150,
	1,0,5,144,6,3,136,6,1,1,188,17,3,173,2,111,0,0,46,4,53,219,1,0,
	170,6,128,2,6,196,6,7,41,149,1,0,8,144,6,3,136,6,1,173,2,220,1,0,
	3,173,2,151,1,0,0,46,0,0,9,156,1,0,0,16,152,1,0,44,2,240,4,1,
	1,23,0,0,0,0,0,0,172,7,3,1,86,0,0,16,152,1,0,44,2,240,4,1,
	173,2,111,0,4,188,4,0,0,0,0,0,0,136,7,3,196,6,7,136,6,1,1,212,
	3,3,173,2,0,0,0,8,144,6,3,136,6,1,173,2,72,0,3,144,6,3,136,6,
	1,173,2,221,1,0,3,173,2,2,0,0,46,2,41,147,1,0,0,0,46,2,1,242,
	1,0,9,248,1,0,0,0,1,190,31,0,44,6,1,203,12,1,181,18,1,162,31,0,
	189,9,222,1,0,208,7,1,173,2,23,0,4,1,188,31,6,254,6,8,48,6,132,7,
	40,206,7,16,1,51,2,200,7,1,173,2,2,0,4,1,173,3,5,1,248,40,0,0,
	0,160,7,3,144,6,3,136,6,1,173,2,152,1,0,3,173,2,103,0,0,46,2,136,
	6,1,173,2,87,0,3,173,2,1,0,0,0,5,206,7,16,1,51,2,200,7,1,248,
	5,49,4,252,5,1,173,2,88,0,0,252,5,1,173,2,49,0,0,0,0,0,23,254,
	6,8,48,2,136,7,3,196,6,7,140,6,3,173,2,2,0,25,173,2,88,0,0,8,
	144,6,3,136,6,1,173,2,89,0,3,173,2,90,0,0,46,2,173,2,49,0,0,9,
	248,1,0,0,0,0,0,1,163,38,0,44,4,1,224,26,1,244,19,0,189,9,223,1,
	0,208,7,1,173,2,26,0,4,1,155,47,6,254,6,8,48,8,140,7,3,190,6,64,
	53,26,144,6,3,173,2,34,0,46,2,173,2,2,0,0,0,21,188,7,3,148,6,3,
	1,134,41,46,2,196,6,7,196,6,7,196,6,7,196,6,7,41,224,1,0,8,136,6,
	1,173,2,25,0,3,160,3,0,0,9,156,1,0,0,8,41,112,0,9,156,1,0,0,
	8,1,139,20,9,156,1,0,0,8,41,45,0,9,156,1,0,0,0,0,0,1,239,16,
	160,7,3,144,6,3,136,6,1,1,135,2,3,136,6,1,173,2,103,0,3,173,2,10,
	0,0,0,46,2,173,2,2,0,0,5,1,133,2,23,254,6,8,48,2,136,7,3,196,
	6,7,140,6,3,136,6,1,173,2,87,0,3,173,2,0,0,0,25,173,2,88,0,0,
	8,144,6,3,136,6,1,173,2,60,0,3,173,2,90,0,0,46,2,173,2,49,0,0,
	9,248,1,0,0,0,0,0,1,238,34,0,44,6,1,251,15,1,143,40,1,210,34,0,
	189,9,225,1,0,208,7,1,173,2,23,0,4,1,155,40,6,254,6,8,48,4,140,7,
	3,196,6,7,136,6,1,173,2,39,0,3,136,6,1,173,2,31,0,3,173,2,1,0,
	0,0,8,173,2,40,0,9,144,1,0,0,21,188,7,3,148,6,3,1,137,43,46,0,
	0,0,0,180,7,3,1,50,39,236,7,54,14,212,8,3,173,2,40,0,48,2,1,33,
	0,212,8,3,41,34,0,48,2,172,7,3,136,6,1,173,2,34,0,3,1,80,0,0,
	0,212,8,3,41,35,0,48,2,172,7,3,144,3,0,0,0,212,8,3,41,65,0,48,
	6,132,7,40,206,7,16,1,51,2,200,7,1,173,2,91,0,5,136,6,1,173,2,65,
	0,3,1,134,1,0,0,0,0,140,7,3,196,6,7,196,6,7,176,6,3,173,2,91,
	0,0,8,41,153,1,0,9,136,1,0,0,8,190,6,64,53,26,144,6,3,136,6,1,
	173,2,226,1,0,3,1,130,20,0,46,2,173,2,91,0,0,0,9,220,1,0,0,21,
	1,179,1,0,172,7,3,173,2,91,0,0,0,212,8,3,41,58,0,48,2,172,7,3,
	136,6,1,173,2,58,0,3,1,236,1,0,0,0,212,8,3,41,32,0,48,6,132,7,
	40,206,7,16,1,51,2,200,7,1,173,2,113,0,5,144,6,3,136,6,1,173,2,82,
	0,3,136,6,1,173,2,87,0,3,160,3,0,0,0,46,2,170,6,128,2,6,196,6,
	7,136,6,1,173,2,154,1,0,3,173,2,92,0,0,8,33,62,0,9,144,1,0,0,
	16,152,1,0,44,2,240,4,1,173,2,92,0,0,0,0,0,0,0,140,7,3,196,6,
	7,138,6,16,32,1,173,2,31,0,3,173,2,113,0,15,112,0,0,8,41,1,0,9,
	148,1,0,0,21,1,224,2,0,172,7,3,144,6,3,136,6,1,173,2,89,0,3,144,
	6,3,136,6,1,173,2,42,0,3,173,2,113,0,0,46,0,0,0,46,2,136,6,1,
	173,2,32,0,3,1,169,3,0,0,0,0,212,8,3,41,93,0,48,6,132,7,40,206,
	7,16,1,51,2,200,7,1,173,2,114,0,5,144,6,3,1,190,1,46,2,170,6,128,
	2,6,196,6,7,1,172,1,8,33,41,0,9,144,1,0,0,16,152,1,0,44,2,1,
	161,1,0,0,0,0,0,140,7,3,196,6,7,138,6,16,32,1,173,2,31,0,3,173,
	2,114,0,15,112,0,0,8,41,1,0,9,148,1,0,0,21,1,252,3,0,172,7,3,
	144,6,3,136,6,1,173,2,89,0,3,144,6,3,136,6,1,173,2,42,0,3,173,2,
	114,0,0,46,0,0,0,46,2,136,6,1,173,2,93,0,3,1,197,4,0,0,0,0,
	0,0,0,44,4,1,208,33,1,182,11,0,189,9,227,1,0,208,7,1,173,2,26,0,
	4,1,139,54,6,254,6,8,48,6,1,252,22,180,7,3,1,219,26,39,236,7,54,10,
	212,8,3,41,153,1,0,48,4,136,7,3,196,6,7,136,6,1,173,2,31,0,3,173,
	2,0,0,0,8,132,6,50,4,228,8,1,173,2,39,0,5,41,65,0,0,228,8,1,
	173,2,65,0,5,173,2,2,0,0,0,9,248,1,0,0,0,168,7,0,0,212,8,3,
	41,50,0,48,4,136,7,3,196,6,7,1,68,8,132,6,50,4,228,8,1,173,2,39,
	0,5,41,58,0,0,228,8,1,173,2,58,0,5,173,2,2,0,0,0,9,248,1,0,
	0,0,168,7,0,0,212,8,3,41,228,1,0,48,4,136,7,3,196,6,7,1,126,8,
	132,6,50,4,228,8,1,173,2,39,0,5,41,34,0,0,228,8,1,173,2,34,0,5,
	173,2,2,0,0,0,9,248,1,0,0,0,168,7,0,0,212,8,3,41,229,1,0,48,
	4,140,7,3,196,6,7,173,2,2,0,8,144,3,0,9,144,1,0,0,21,254,6,8,
	48,2,136,7,3,196,6,7,1,209,1,8,132,6,50,4,228,8,1,173,2,39,0,5,
	41,35,0,0,228,8,1,173,2,35,0,5,136,6,1,173,2,230,1,0,3,173,2,231,
	1,0,0,0,0,9,248,1,0,0,0,0,22,140,7,3,144,6,3,136,6,1,173,2,
	155,1,0,3,136,6,1,173,2,156,1,0,3,173,2,10,0,0,0,46,2,173,2,2,
	0,0,21,254,6,8,48,2,136,7,3,196,6,7,1,183,2,8,132,6,50,4,228,8,
	1,173,2,39,0,5,41,32,0,0,228,8,1,173,2,32,0,5,144,6,3,136,6,1,
	173,2,60,0,3,173,2,232,1,0,0,46,2,173,2,2,0,0,0,0,9,248,1,0,
	0,0,0,22,254,6,8,48,2,136,7,3,196,6,7,1,128,3,8,132,6,50,4,228,
	8,1,173,2,39,0,5,41,93,0,0,228,8,1,173,2,93,0,5,144,6,3,136,6,
	1,173,2,60,0,3,173,2,233,1,0,0,46,2,173,2,2,0,0,0,0,9,248,1,
	0,0,0,0,0,0,168,7,0,0,216,8,48,2,188,7,3,148,6,3,1,194,51,46,
	2,196,6,7,196,6,7,196,6,7,41,157,1,0,8,1,185,10,9,156,1,0,0,8,
	41,112,0,9,156,1,0,0,8,144,6,3,173,2,27,0,46,2,173,2,2,0,0,9,
	156,1,0,0,0,0,0,0,0,1,183,44,0,44,6,240,4,1,173,2,2,0,4,1,
	213,49,0,1,183,31,1,164,44,0,189,9,234,1,0,208,7,1,173,2,23,0,4,1,
	237,49,6,254,6,8,48,2,172,7,3,144,6,3,136,6,1,173,2,72,0,3,136,6,
	1,173,2,94,0,3,173,2,1,0,0,0,46,2,170,6,128,2,6,1,171,12,16,152,
	1,0,44,2,240,4,1,173,2,49,0,0,0,0,0,0,44,4,1,253,38,1,227,16,
	0,189,9,235,1,0,208,7,1,173,2,26,0,4,1,184,59,6,254,6,8,48,10,140,
	7,3,190,6,64,53,26,1,143,3,0,21,1,214,1,0,1,186,28,132,7,40,206,7,
	16,1,51,2,200,7,1,173,2,94,0,5,144,6,3,136,6,1,173,2,72,0,3,173,
	2,2,0,0,46,2,170,6,128,2,6,1,170,11,16,152,1,0,44,2,1,112,0,0,
	0,0,0,136,7,3,144,6,3,136,6,1,173,2,158,1,0,3,136,6,1,173,2,94,
	0,3,173,2,0,0,0,0,46,2,212,6,3,173,2,94,0,0,0,0,1,178,46,0,
	44,6,1,251,1,1,169,33,1,150,46,0,189,9,236,1,0,208,7,1,173,2,23,0,
	4,1,223,51,6,254,6,8,48,2,172,7,3,136,6,1,173,2,4,0,3,173,2,1,
	0,0,0,0,44,4,1,201,40,1,175,18,0,189,9,237,1,0,208,7,1,173,2,26,
	0,4,1,132,61,6,254,6,8,48,6,1,245,29,136,7,3,196,6,7,1,179,47,8,
	228,6,3,144,6,3,136,6,1,173,2,29,0,3,136,6,1,173,2,44,0,3,160,3,
	0,0,0,46,8,173,2,2,0,33,108,0,173,2,40,0,41,4,0,0,4,128,4,0,
	0,9,248,1,0,0,0,1,205,47,0,44,6,1,150,3,1,196,34,1,177,47,0,189,
	9,238,1,0,1,155,1,189,9,239,1,0,208,7,1,173,2,26,0,4,1,246,61,6,
	254,6,8,48,6,1,231,30,136,7,3,196,6,7,1,165,48,8,228,6,3,144,6,3,
	1,114,46,8,173,2,2,0,33,70,0,173,2,40,0,41,4,0,0,4,188,4,0,0,
	9,248,1,0,0,0,1,172,48,0,44,6,1,245,3,1,163,35,1,144,48,0,189,9,
	240,1,0,208,7,1,173,2,23,0,4,1,217,53,6,254,6,8,48,2,172,7,3,144,
	6,3,136,6,1,173,2,29,0,3,136,6,1,173,2,57,0,3,160,3,0,0,0,46,
	10,33,38,0,1,151,2,41,4,0,236,2,0,168,3,0,0,0,0,44,4,1,224,42,
	1,198,20,0,189,9,241,1,0,208,7,1,173,2,26,0,4,1,155,63,6,254,6,8,
	48,6,1,140,32,136,7,3,196,6,7,1,202,49,8,228,6,3,144,6,3,1,151,2,
	46,8,173,2,2,0,33,21,0,173,2,40,0,41,4,0,0,4,176,4,0,0,9,248,
	1,0,0,0,1,210,49,0,44,6,1,155,5,1,201,36,1,182,49,0,189,9,242,1,
	0,208,7,1,173,2,23,0,4,1,255,54,6,254,6,8,48,2,172,7,3,144,6,3,
	1,166,1,46,10,33,21,0,1,171,3,41,4,0,236,2,0,168,3,0,0,0,0,44,
	4,1,244,43,1,218,21,0,189,9,243,1,0,1,148,1,189,9,244,1,0,208,7,1,
	173,2,23,0,4,1,195,55,6,254,6,8,48,2,172,7,3,144,6,3,1,234,1,46,
	10,33,41,0,1,239,3,41,4,0,236,2,0,168,3,0,0,0,0,44,4,1,184,44,
	1,158,22,0,189,9,245,1,0,208,7,1,173,2,26,0,4,1,243,64,6,254,6,8,
	48,6,1,228,33,136,7,3,196,6,7,1,162,51,8,228,6,3,144,6,3,1,239,3,
	46,8,173,2,2,0,33,41,0,173,2,40,0,41,4,0,0,4,176,4,0,0,9,248,
	1,0,0,0,1,170,51,0,44,6,1,243,6,1,161,38,1,142,51,0,189,9,246,1,
	0,208,7,1,173,2,23,0,4,1,215,56,6,254,6,8,48,2,172,7,3,144,6,3,
	1,254,2,46,10,33,159,1,0,1,132,5,41,4,0,236,2,0,168,3,0,0,0,0,
	44,4,1,205,45,1,179,23,0,189,9,247,1,0,208,7,1,173,2,26,0,4,1,136,
	66,6,254,6,8,48,6,1,249,34,136,7,3,196,6,7,1,183,52,8,228,6,3,144,
	6,3,1,132,5,46,8,173,2,2,0,33,159,1,0,173,2,40,0,41,4,0,0,4,
	176,4,0,0,9,248,1,0,0,0,1,192,52,0,44,6,1,137,8,1,183,39,1,164,
	52,0,189,9,248,1,0,208,7,1,173,2,23,0,4,1,190,52,6,254,6,8,48,2,
	172,7,3,144,6,3,1,148,4,46,10,136,6,1,173,2,160,1,0,3,173,2,35,0,
	0,1,164,6,41,4,0,236,2,0,168,3,0,0,0,0,44,4,1,237,46,1,129,40,
	0,189,9,249,1,0,208,7,1,173,2,26,0,4,1,168,67,6,254,6,8,48,6,1,
	153,36,136,7,3,196,6,7,1,215,53,8,228,6,3,144,6,3,1,164,6,46,8,173,
	2,2,0,1,88,136,6,1,173,2,34,0,3,173,2,11,0,0,41,4,0,0,4,244,
	3,0,0,9,248,1,0,0,0,1,231,53,0,44,6,1,244,34,1,136,59,1,203,53,
	0,189,9,250,1,0,208,7,1,173,2,23,0,4,1,229,53,6,254,6,8,48,2,172,
	7,3,144,6,3,1,187,5,46,10,136,6,1,173,2,161,1,0,3,173,2,35,0,0,
	1,203,7,41,4,0,236,2,0,168,3,0,0,0,0,44,4,1,148,48,1,168,41,0,
	189,9,251,1,0,208,7,1,173,2,26,0,4,1,207,68,6,254,6,8,48,6,1,192,
	37,136,7,3,196,6,7,1,254,54,8,228,6,3,144,6,3,1,203,7,46,8,173,2,
	2,0,1,88,1,167,1,41,4,0,0,4,244,3,0,0,9,248,1,0,0,0,1,132,
	55,0,44,6,1,145,36,1,165,60,1,232,54,0,189,9,252,1,0,208,7,1,173,2,
	23,0,4,1,177,60,6,254,6,8,48,2,172,7,3,144,6,3,1,216,6,46,10,33,
	162,1,0,1,222,8,41,4,0,236,2,0,168,3,0,0,0,0,44,4,1,167,49,1,
	141,27,0,189,9,253,1,0,208,7,1,173,2,26,0,4,1,226,69,6,254,6,8,48,
	6,1,211,38,136,7,3,196,6,7,1,145,56,8,228,6,3,144,6,3,1,222,8,46,
	8,173,2,2,0,33,162,1,0,173,2,40,0,41,4,0,0,4,164,5,35,173,2,254,
	1,0,0,0,9,248,1,0,0,0,1,160,56,0,44,6,1,233,11,1,151,43,1,132,
	56,0,189,9,255,1,0,208,7,1,173,2,128,2,0,4,188,4,0,6,254,6,8,48,
	6,132,7,40,206,7,16,1,51,2,200,7,1,173,2,8,0,5,130,6,8,49,7,144,
	6,3,136,6,1,173,2,54,0,3,136,6,1,173,2,74,0,3,173,2,1,0,0,0,
	46,2,33,22,0,0,144,6,3,136,6,1,173,2,54,0,3,136,6,1,173,2,75,0,
	3,173,2,1,0,0,0,46,2,33,22,0,0,144,6,3,136,6,1,173,2,54,0,3,
	136,6,1,173,2,76,0,3,173,2,1,0,0,0,46,2,33,22,0,0,0,0,0,0,
	140,7,3,136,6,1,173,2,64,0,3,173,2,1,0,0,21,254,6,8,48,4,132,7,
	40,206,7,16,1,51,2,200,7,1,173,2,64,0,5,144,6,3,136,6,1,173,2,129,
	2,0,3,173,2,53,0,0,46,4,144,6,3,136,6,1,173,2,130,2,0,3,173,2,
	53,0,0,46,4,136,6,1,173,2,4,0,3,1,82,0,33,21,0,0,33,14,0,0,
	0,0,0,136,7,3,144,6,3,136,6,1,1,152,12,3,173,2,8,0,0,46,2,144,
	6,3,136,6,1,173,2,54,0,3,144,6,3,136,6,1,173,2,131,2,0,3,173,2,
	53,0,0,46,2,196,6,7,173,2,64,0,8,33,115,0,9,164,1,0,0,0,0,46,
	2,33,22,0,0,0,0,0,0,172,7,3,196,6,7,41,132,2,0,8,144,6,3,136,
	6,1,1,228,31,3,144,6,3,136,6,1,173,2,72,0,3,173,2,8,0,0,46,2,
	170,6,128,2,6,200,6,10,228,1,0,11,232,1,0,18,196,6,7,136,6,1,173,2,
	46,0,3,173,2,95,0,0,8,33,38,0,9,116,0,0,19,196,6,7,41,14,0,8,
	173,2,95,0,9,156,1,0,0,20,173,2,95,0,0,16,152,1,0,44,2,240,4,1,
	173,2,95,0,0,0,0,0,46,2,41,81,0,0,9,156,1,0,0,0,0,44,2,1,
	210,53,0,189,9,133,2,0,208,7,1,173,2,134,2,0,4,1,139,74,6,254,6,8,
	48,4,140,7,3,144,6,3,136,6,1,173,2,73,0,3,53,135,2,0,0,46,2,173,
	2,8,0,0,21,254,6,8,48,2,172,7,3,134,6,8,50,7,228,8,1,173,2,74,
	0,5,144,6,3,173,2,28,0,46,4,196,6,7,144,6,3,136,6,1,173,2,6,0,
	3,173,2,8,0,0,46,4,33,21,0,33,38,0,0,8,1,26,9,156,1,0,0,33,
	22,0,0,0,228,8,1,173,2,75,0,5,144,6,3,173,2,28,0,46,4,196,6,7,
	144,6,3,1,58,46,4,33,38,0,33,24,0,0,8,1,15,9,156,1,0,0,33,22,
	0,0,0,228,8,1,173,2,76,0,5,144,6,3,173,2,28,0,46,4,196,6,7,144,
	6,3,1,105,46,4,33,24,0,33,55,0,0,8,1,15,9,156,1,0,0,33,22,0,
	0,0,0,0,0,22,140,7,3,144,6,3,136,6,1,173,2,73,0,3,53,136,2,0,
	0,46,2,173,2,8,0,0,21,254,6,8,48,2,172,7,3,134,6,8,50,7,228,8,
	1,173,2,74,0,5,144,6,3,173,2,28,0,46,4,144,6,3,1,193,1,46,4,33,
	21,0,33,24,0,0,33,22,0,0,0,228,8,1,173,2,75,0,5,144,6,3,173,2,
	28,0,46,4,144,6,3,1,230,1,46,4,33,24,0,33,41,0,0,33,22,0,0,0,
	228,8,1,173,2,76,0,5,144,6,3,173,2,28,0,46,4,144,6,3,1,139,2,46,
	4,33,41,0,33,63,0,0,33,22,0,0,0,0,0,0,22,140,7,3,144,6,3,136,
	6,1,173,2,73,0,3,53,137,2,0,0,46,2,173,2,8,0,0,21,254,6,8,48,
	2,172,7,3,134,6,8,50,8,1,223,2,1,168,2,1,252,1,228,8,1,173,2,64,
	0,5,134,6,8,50,3,228,8,1,173,2,4,0,5,196,6,7,144,6,3,173,2,28,
	0,46,4,196,6,7,144,6,3,1,248,2,46,4,33,55,0,33,41,0,0,8,1,16,
	9,156,1,0,0,33,22,0,0,8,33,115,0,9,172,1,0,0,0,0,0,0,0,0,
	22,140,7,3,144,6,3,136,6,1,173,2,73,0,3,53,138,2,0,0,46,2,173,2,
	8,0,0,21,254,6,8,48,2,172,7,3,134,6,8,50,8,1,155,2,1,249,1,1,
	215,1,228,8,1,173,2,64,0,5,134,6,8,50,3,228,8,1,173,2,4,0,5,196,
	6,7,144,6,3,173,2,28,0,46,4,144,6,3,1,245,3,46,4,33,63,0,33,70,
	0,0,33,22,0,0,8,33,115,0,9,172,1,0,0,0,0,0,0,0,0,0,0,0,
	0,188,7,3,148,6,3,173,2,7,0,46,2,41,139,2,0,0,0,0,44,2,240,4,
	1,173,2,8,0,4,188,4,0,0,0,189,9,140,2,0,208,7,1,173,2,163,1,0,
	4,164,5,35,228,4,7,173,2,10,0,8,173,2,15,0,0,0,6,254,6,8,48,4,
	132,7,40,206,7,16,1,51,2,200,7,1,173,2,47,0,5,148,6,3,136,6,1,173,
	2,15,0,3,173,2,10,0,0,46,0,0,0,0,0,172,7,3,148,6,3,1,25,46,
	15,136,6,1,173,2,96,0,3,173,2,1,0,0,196,6,7,136,6,1,173,2,97,0,
	3,173,2,1,0,0,8,33,21,0,9,160,1,0,0,136,6,1,173,2,98,0,3,173,
	2,1,0,0,196,6,7,173,2,77,0,8,144,6,3,136,6,1,173,2,116,0,3,173,
	2,47,0,0,46,0,0,9,240,1,0,0,196,6,7,173,2,78,0,8,144,6,3,136,
	6,1,173,2,117,0,3,173,2,47,0,0,46,0,0,9,240,1,0,0,196,6,7,173,
	2,30,0,8,144,6,3,136,6,1,173,2,118,0,3,173,2,47,0,0,46,0,0,9,
	240,1,0,0,196,6,7,173,2,19,0,8,144,6,3,136,6,1,173,2,119,0,3,173,
	2,47,0,0,46,0,0,9,240,1,0,0,0,0,0,44,10,1,244,60,240,4,1,173,
	2,77,0,4,176,4,0,10,228,1,0,0,240,4,1,173,2,78,0,4,176,4,0,10,
	228,1,0,0,240,4,1,173,2,30,0,4,176,4,0,10,228,1,0,0,240,4,1,173,
	2,19,0,4,176,4,0,10,228,1,0,0,0,189,9,141,2,0,208,7,1,173,2,99,
	0,4,1,236,81,6,254,6,8,48,2,172,7,3,134,6,8,50,7,228,8,1,173,2,
	96,0,5,144,6,3,136,6,1,173,2,164,1,0,3,173,2,17,0,0,46,0,0,0,
	228,8,1,173,2,97,0,5,196,6,7,144,6,3,136,6,1,173,2,165,1,0,3,173,
	2,17,0,0,46,0,0,8,33,21,0,9,156,1,0,0,0,228,8,1,173,2,98,0,
	5,144,6,3,136,6,1,173,2,166,1,0,3,173,2,17,0,0,46,0,0,0,0,0,
	0,44,2,240,4,1,173,2,17,0,4,1,210,3,0,0,189,9,142,2,0,208,7,1,
	173,2,47,0,4,1,255,82,6,254,6,8,48,2,172,7,3,144,6,3,136,6,1,173,
	2,99,0,3,160,3,0,0,46,2,1,217,3,0,0,0,44,0,0,189,9,143,2,0,
	208,7,1,1,152,4,4,1,150,4,6,254,6,8,48,6,132,7,40,206,7,16,1,51,
	4,200,7,1,173,2,100,0,5,148,6,3,1,136,4,46,15,1,240,3,1,230,3,1,
	208,3,136,6,1,173,2,77,0,3,173,2,1,0,0,136,6,1,173,2,78,0,3,173,
	2,1,0,0,1,162,64,196,6,7,1,246,63,8,33,36,0,9,172,1,0,0,0,0,
	200,7,1,173,2,79,0,5,136,6,1,173,2,9,0,3,173,2,1,0,0,0,0,0,
	140,7,3,173,2,79,0,21,254,6,8,48,4,140,7,3,196,6,7,136,6,1,173,2,
	39,0,3,173,2,79,0,0,8,41,27,0,9,144,1,0,0,21,188,7,3,148,6,3,
	1,150,78,46,2,41,144,2,0,0,0,0,140,7,3,196,6,7,1,47,8,41,13,0,
	9,144,1,0,0,21,254,6,8,48,4,132,7,40,206,7,16,1,51,2,200,7,1,173,
	2,56,0,5,144,6,3,136,6,1,173,2,61,0,3,144,6,3,1,133,67,46,2,136,
	6,1,173,2,30,0,3,136,6,1,173,2,13,0,3,173,2,79,0,0,0,0,0,46,
	0,0,0,0,0,136,7,3,196,6,7,173,2,100,0,8,148,6,3,1,255,5,46,2,
	196,6,7,144,6,3,136,6,1,1,238,64,3,173,2,100,0,0,46,0,0,8,160,6,
	3,196,6,7,173,2,56,0,8,33,36,0,9,164,1,0,0,0,9,160,1,0,0,0,
	9,248,1,0,0,0,0,0,0,0,172,7,3,173,2,100,0,0,0,44,2,1,232,65,
	0,189,9,145,2,0,208,7,1,173,2,99,0,4,1,160,86,6,254,6,8,48,2,172,
	7,3,134,6,8,50,16,1,180,4,1,154,4,1,244,3,228,8,1,173,2,77,0,5,
	144,6,3,136,6,1,173,2,116,0,3,173,2,17,0,0,46,0,0,0,228,8,1,173,
	2,78,0,5,144,6,3,136,6,1,173,2,117,0,3,173,2,17,0,0,46,0,0,0,
	228,8,1,173,2,30,0,5,144,6,3,136,6,1,173,2,118,0,3,173,2,17,0,0,
	46,0,0,0,228,8,1,173,2,16,0,5,196,6,7,144,6,3,136,6,1,173,2,119,
	0,3,173,2,17,0,0,46,0,0,8,33,36,0,9,164,1,0,0,0,228,8,1,173,
	2,9,0,5,134,6,8,50,4,228,8,1,173,2,39,0,5,41,13,0,0,228,8,1,
	173,2,13,0,5,134,6,8,50,5,228,8,1,173,2,30,0,5,144,6,3,136,6,1,
	173,2,35,0,3,144,6,3,1,227,69,46,2,196,6,7,144,6,3,136,6,1,173,2,
	146,2,0,3,173,2,17,0,0,46,0,0,8,33,147,2,0,9,164,1,0,0,0,0,
	46,0,0,0,228,8,1,173,2,16,0,5,33,14,0,0,0,0,0,0,0,0,0,44,
	2,1,206,5,0,189,9,148,2,0,208,7,1,173,2,99,0,4,1,196,88,6,254,6,
	8,48,2,172,7,3,134,6,8,50,9,1,155,2,1,130,2,1,233,1,1,208,1,0,
	0,0,44,2,1,129,6,0,
};

static const SnippetHandler snippet_handlers[] = {
	{"google.protobuf.Any", "S,T:IMessageType,T:BinaryReadOptions,T:JsonWriteOptions,T:JsonReadOptions,N:jsonWriteOptions,T:JsonValue,N:typeofJsonValue,N:isJsonObject,L:type_url", 0, 7},
	{"google.protobuf.Timestamp", "S,N:PbLong,T:JsonWriteOptions,T:JsonReadOptions,T:JsonValue,N:typeofJsonValue,C", 2215, 5},
	{"google.protobuf.Duration", "S,N:PbLong,T:JsonWriteOptions,T:JsonReadOptions,T:JsonValue,N:typeofJsonValue,C", 4254, 2},
	{"google.protobuf.FieldMask", "S,T:JsonWriteOptions,T:JsonReadOptions,N:lowerCamelCase,T:JsonValue,N:typeofJsonValue", 5238, 2},
	{"google.protobuf.Struct", "S,T:JsonObject,T:JsonWriteOptions,T:JsonReadOptions,T:JsonValue,N:typeofJsonValue,N:isJsonObject", 5856, 2},
	{"google.protobuf.Value", "S,T:JsonWriteOptions,T:JsonReadOptions,T:JsonValue,N:typeofJsonValue,L:null_value,L:bool_value,L:number_value,L:string_value,L:list_value,L:struct_value", 6288, 2},
	{"google.protobuf.ListValue", "S,T:JsonWriteOptions,T:JsonReadOptions,T:JsonValue,N:typeofJsonValue", 7522, 2},
	{"google.protobuf.BoolValue", "S,T:JsonWriteOptions,T:JsonReadOptions,T:JsonValue", 7764, 2},
	{"google.protobuf.StringValue", "S,T:JsonWriteOptions,T:JsonReadOptions,T:JsonValue", 7919, 2},
	{"google.protobuf.DoubleValue", "S,T:JsonWriteOptions,T:JsonReadOptions,T:JsonValue", 8014, 2},
	{"google.protobuf.FloatValue", "S,T:JsonWriteOptions,T:JsonReadOptions,T:JsonValue", 8180, 2},
	{"google.protobuf.Int32Value", "S,T:JsonWriteOptions,T:JsonReadOptions,T:JsonValue", 8248, 2},
	{"google.protobuf.UInt32Value", "S,T:JsonWriteOptions,T:JsonReadOptions,T:JsonValue", 8396, 2},
	{"google.protobuf.Int64Value", "S,N:LongType,T:JsonWriteOptions,T:JsonReadOptions,T:JsonValue,N:ScalarType,E", 8546, 2},
	{"google.protobuf.UInt64Value", "S,N:LongType,T:JsonWriteOptions,T:JsonReadOptions,T:JsonValue,N:ScalarType,E", 8713, 2},
	{"google.protobuf.BytesValue", "S,T:JsonWriteOptions,T:JsonReadOptions,T:JsonValue", 8870, 2},
	{"google.type.Color", "S", 9026, 2},
	{"google.type.Date", "S", 10139, 2},
	{"google.type.DateTime", "S,N:PbLong,L:utc_offset,L:time_offset,L:time_zone,C", 10624, 3},
	{"google.type.TimeOfDay", "S", 11333, 1},
};
