/* Copyright (c) 2014 - 2024 Apple Inc. and the project authors
 * Licensed under Apache License v2.0 with Runtime Library Exception
 *
 * See LICENSE.txt for license information:
 * https://github.com/apple/swift-protobuf/blob/main/LICENSE.txt */

/* Port of Sources/SwiftProtobufPluginLibrary/PluginLibEditionDefaults.swift of swift-protobuf
 * 1.37.0, the only part of SwiftProtobufPluginLibrary that differs from 1.39.0 (descriptor.c):
 * bundledFeatureSetDefaultBytes decoded, with editions up to 2024, an UNSTABLE entry and no
 * feature 9. */
#include "swift_protobuf.h"

static const SwEditionDefault defaults[] = {
	{SW_EDITION_LEGACY, {{0, 1, 2, 2, 3, 1, 2, 2, 1, 0}}},
	{SW_EDITION_PROTO3, {{0, 2, 1, 1, 2, 1, 1, 2, 1, 0}}},
	{SW_EDITION_2023, {{0, 1, 1, 1, 2, 1, 1, 2, 1, 0}}},
	{SW_EDITION_2024, {{0, 1, 1, 1, 2, 1, 1, 1, 2, 0}}},
	{SW_EDITION_UNSTABLE, {{0, 1, 1, 1, 2, 1, 1, 3, 2, 0}}},
};

const SwEditionDefaults sw_edition_defaults_1_37_0 = {
	SW_EDITION_PROTO2, SW_EDITION_2024, (int)(sizeof defaults / sizeof defaults[0]), defaults,
};
