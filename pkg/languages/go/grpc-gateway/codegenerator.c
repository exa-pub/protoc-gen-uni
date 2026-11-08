/* Copyright (c) 2015, Gengo, Inc. All rights reserved. BSD-3-Clause (see LICENSE of
 * github.com/grpc-ecosystem/grpc-gateway). */

/* Port of grpc-gateway v2.33.0 internal/codegenerator/supported_features.go. */
#include "grpcgateway.h"

/* SetSupportedFeaturesOnPluginGen sets supported proto3 features on protogen.Plugin: proto3
 * optional and editions, up to edition 2026. */
void gw_codegenerator_SetSupportedFeaturesOnPluginGen(PgPlugin *gen)
{
	gen->SupportedFeatures = GW_SUPPORTED_FEATURES;
	gen->SupportedEditionsMinimum = GW_EDITIONS_MINIMUM;
	gen->SupportedEditionsMaximum = GW_EDITIONS_MAXIMUM;
}
