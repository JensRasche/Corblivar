/*
 * =====================================================================================
 *
 *    Description:  Corblivar thermal analyzer, based on power blurring
 *
 *         Author:  Johann Knechtel, johann.knechtel@ifte.de
 *        Company:  Institute of Electromechanical and Electronic Design, www.ifte.de
 *
 * =====================================================================================
 */

// own Corblivar header
#include "ThermalAnalyzer.hpp"
// required Corblivar headers
#include "Rect.hpp"
#include "Block.hpp"
#include "Math.hpp"

// memory allocation
constexpr double ThermalAnalyzer::ROOM_TEMPERATURE_K;
constexpr int ThermalAnalyzer::POWER_MAPS_DIM;

// Thermal-analyzer routine based on power blurring,
// i.e., convolution of thermals masks and power maps into thermal maps.
// Based on a separated convolution using separated 2D gauss function, i.e., 1D gauss fct.
// Returns max value of thermal map of lowest layer, i.e., hottest layer
// Based on http://www.songho.ca/dsp/convolution/convolution.html#separable_convolution
double ThermalAnalyzer::performPowerBlurring(int const& layers, double& max_cost_temp, bool const& set_max_cost, bool const& normalize) {
	int layer;
	int x, y, i;
	int map_x, map_y;
	int mask_i;
	double max_temp;
	// required as buffer for separated convolution; note that its dimensions
	// corresponds to a power map, which is required to hold temporary results for 1D
	// convolution of padded power maps
	array<array<double,ThermalAnalyzer::POWER_MAPS_DIM>,ThermalAnalyzer::POWER_MAPS_DIM> thermal_map_tmp;

	if (ThermalAnalyzer::DBG_CALLS) {
		cout << "-> ThermalAnalyzer::performPowerBlurring(" << layers << ", " << max_cost_temp << ", ";
		cout << set_max_cost << ", " << normalize << ")" << endl;
	}

	// init temp map w/ zero
	for (auto& m : thermal_map_tmp) {
		m.fill(0.0);
	}

	// reset final map to room temperature
	for (auto& m : this->thermal_map) {
		m.fill(ThermalAnalyzer::ROOM_TEMPERATURE_K);
	}

	/// perform 2D convolution by performing two separated 1D convolution iterations;
	/// note that no (kernel) flipping is required since the mask is symmetric
	//
	// start w/ horizontal convolution (with which to start doesn't matter actually)
	for (layer = 0; layer < layers; layer++) {

		// walk power-map grid for horizontal convolution; store into
		// thermal_map_tmp; note that during horizontal convolution we need to
		// walk the full y-dimension related to the padded power map in order to
		// reasonably model the thermal effect in the padding zone during
		// subsequent vertical convolution
		for (y = 0; y < ThermalAnalyzer::POWER_MAPS_DIM; y++) {

			// for the x-dimension during horizontal convolution, we need to
			// restrict the considered range according to the thermal map in
			// order to exploit the padded power map w/o mask boundary checks
			for (x = ThermalAnalyzer::POWER_MAPS_PADDED_BINS; x < ThermalAnalyzer::THERMAL_MAP_DIM + ThermalAnalyzer::POWER_MAPS_PADDED_BINS; x++) {

				// perform horizontal 1D convolution, i.e., multiply
				// input[x] w/ mask
				//
				// e.g., for x = 0, THERMAL_MASK_DIM = 3
				// convol1D(x=0) = input[-1] * mask[0] + input[0] * mask[1] + input[1] * mask[2]
				//
				// can be also illustrated by aligning and multiplying
				// both arrays:
				// input array (power map); unpadded view
				// |x=-1|x=0|x=1|x=2|
				// input array (power map); padded, real view
				// |x=0 |x=1|x=2|x=3|
				// mask:
				// |m=0 |m=1|m=2|
				//
				for (mask_i = 0; mask_i < ThermalAnalyzer::THERMAL_MASK_DIM; mask_i++) {

					// determine power-map index; note that it is not
					// out of range due to the padded power maps
					i = x + (mask_i - ThermalAnalyzer::THERMAL_MASK_CENTER);

					if (ThermalAnalyzer::DBG) {
						cout << "y=" << y << ", x=" << x << ", mask_i=" << mask_i << ", i=" << i << endl;
						if (i < 0 || i >= ThermalAnalyzer::POWER_MAPS_DIM) {
							cout << "i out of range (should be limited by x)" << endl;
						}
					}

					// convolution; multplication of mask element and
					// power-map bin
					thermal_map_tmp[x][y] += this->power_maps[layer][i][y] * this->thermal_masks[layer][mask_i];
				}
			}
		}
	}

	// continue w/ vertical convolution
	for (layer = 0; layer < layers; layer++) {

		// walk power-map grid for vertical convolution; convolute mask w/ data
		// obtained by horizontal convolution (thermal_map_tmp)
		for (x = ThermalAnalyzer::POWER_MAPS_PADDED_BINS; x < ThermalAnalyzer::THERMAL_MAP_DIM + ThermalAnalyzer::POWER_MAPS_PADDED_BINS; x++) {

			// adapt index for final thermal map according to padding
			map_x = x - ThermalAnalyzer::POWER_MAPS_PADDED_BINS;

			for (y = ThermalAnalyzer::POWER_MAPS_PADDED_BINS; y < ThermalAnalyzer::THERMAL_MAP_DIM + ThermalAnalyzer::POWER_MAPS_PADDED_BINS; y++) {

				// adapt index for final thermal map according to padding
				map_y = y - ThermalAnalyzer::POWER_MAPS_PADDED_BINS;

				// perform 1D vertical convolution
				for (mask_i = 0; mask_i < ThermalAnalyzer::THERMAL_MASK_DIM; mask_i++) {

					// determine power-map index; note that it is not
					// out of range due to the padded power maps
					i = y + (mask_i - ThermalAnalyzer::THERMAL_MASK_CENTER);

					if (ThermalAnalyzer::DBG) {
						cout << "x=" << x << ", y=" << y << ", map_x=" << map_x << ", map_y=" << map_y;
						cout << ", mask_i=" << mask_i << ", i=" << i << endl;
						if (i < 0 || i >= ThermalAnalyzer::POWER_MAPS_DIM) {
							cout << "i out of range (should be limited by y)" << endl;
						}
					}

					// convolution; multplication of mask element and
					// power-map bin
					this->thermal_map[map_x][map_y] += thermal_map_tmp[x][i] * this->thermal_masks[layer][mask_i];
				}
			}
		}
	}

	// determine max value
	max_temp = 0.0;
	for (x = 0; x < ThermalAnalyzer::THERMAL_MAP_DIM; x++) {
		for (y = 0; y < ThermalAnalyzer::THERMAL_MAP_DIM; y++) {
			max_temp = max(max_temp, this->thermal_map[x][y]);
		}
	}

	// memorize max cost; initial sampling
	if (set_max_cost) {
		max_cost_temp = max_temp;
	}

	// normalize to max value from initial sampling
	if (normalize) {
		max_temp /= max_cost_temp;
	}

	if (ThermalAnalyzer::DBG_CALLS) {
		cout << "<- ThermalAnalyzer::performPowerBlurring : " << max_temp << endl;
	}

	return max_temp;
}

void ThermalAnalyzer::generatePowerMaps(int const& layers, vector<Block> const& blocks, double const& outline_x, double const& outline_y, double const& power_density_scaling_padding_zone, bool const& extend_boundary_blocks_into_padding_zone) {
	int i;
	int x, y;
	Rect bin, intersect, block_offset;
	int x_lower, x_upper, y_lower, y_upper;
	bool padding_zone;

	if (ThermalAnalyzer::DBG_CALLS) {
		cout << "-> ThermalAnalyzer::generatePowerMaps(" << layers << ", " << &blocks << ", " << outline_x << ", " << outline_y << ", " << power_density_scaling_padding_zone << ", " << extend_boundary_blocks_into_padding_zone << ")" << endl;
	}

	// determine maps for each layer
	for (i = 0; i < layers; i++) {

		// reset map to zero
		// note: this also implicitly pads the map w/ zero
		for (auto& m : this->power_maps[i]) {
			m.fill(0.0);
		}

		// consider each block on the related layer
		for (Block const& block : blocks) {

			// drop blocks assigned to other layers
			if (block.layer != i) {
				continue;
			}

			// determine offset, i.e., shifted, block bb; relates to block's
			// bb in padded power map
			block_offset = block.bb;

			// don't offset blocks at the left/lower chip boundaries,
			// implicitly extend them into power-map padding zone; this way,
			// during convolution, the thermal estimate increases for these
			// blocks; blocks not at the boundaries are shifted
			if (extend_boundary_blocks_into_padding_zone && block.bb.ll.x == 0.0) {
			}
			else {
				block_offset.ll.x += this->blocks_offset_x;
			}
			if (extend_boundary_blocks_into_padding_zone && block.bb.ll.y == 0.0) {
			}
			else {
				block_offset.ll.y += this->blocks_offset_y;
			}

			// also consider extending blocks into right/upper padding zone if
			// they are close to the related chip boundaries
			if (
					extend_boundary_blocks_into_padding_zone &&
					abs(outline_x - block.bb.ur.x) < this->padding_right_boundary_blocks_distance
			   ) {
				block_offset.ur.x = outline_x + 2.0 * this->blocks_offset_x;
			}
			// simple shift otherwise; compensate for padding of left/bottom
			// boundaries
			else {
				block_offset.ur.x += this->blocks_offset_x;
			}

			if (
					extend_boundary_blocks_into_padding_zone
					&& abs(outline_y - block.bb.ur.y) < this->padding_upper_boundary_blocks_distance
			   ) {
				block_offset.ur.y = outline_y + 2.0 * this->blocks_offset_y;
			}
			else {
				block_offset.ur.y += this->blocks_offset_y;
			}

			// determine index boundaries for offset block; based on boundary
			// of blocks and the covered bins; note that cast to int truncates
			// toward zero, i.e., performs like floor for positive numbers
			x_lower = static_cast<int>(block_offset.ll.x / this->power_maps_dim_x);
			y_lower = static_cast<int>(block_offset.ll.y / this->power_maps_dim_y);
			// +1 in order to efficiently obtain the result of ceil() w/o cast
			x_upper = static_cast<int>(block_offset.ur.x / this->power_maps_dim_x) + 1;
			y_upper = static_cast<int>(block_offset.ur.y / this->power_maps_dim_y) + 1;

			// walk power-map bins covering block outline
			for (x = x_lower; x < x_upper; x++) {
				for (y = y_lower; y < y_upper; y++) {

					// determine real coords of map bin
					bin.ll.x = this->power_maps_bins_ll_x[x];
					bin.ll.y = this->power_maps_bins_ll_y[y];
					// note that +1 is guaranteed to be within bounds
					// of power_maps_bins_ll_x/y (size =
					// ThermalAnalyzer::POWER_MAPS_DIM + 1); the
					// related last tuple describes the upper-right
					// corner coordinates of the right/top boundary
					bin.ur.x = this->power_maps_bins_ll_x[x + 1];
					bin.ur.y = this->power_maps_bins_ll_y[y + 1];

					// determine if bin w/in padding zone
					if (
							x < ThermalAnalyzer::POWER_MAPS_PADDED_BINS
							|| x >= (ThermalAnalyzer::POWER_MAPS_DIM - ThermalAnalyzer::POWER_MAPS_PADDED_BINS)
							|| y < ThermalAnalyzer::POWER_MAPS_PADDED_BINS
							|| y >= (ThermalAnalyzer::POWER_MAPS_DIM - ThermalAnalyzer::POWER_MAPS_PADDED_BINS)
					   ) {
						padding_zone = true;
					}
					else {
						padding_zone = false;
					}

					// consider full block power density for fully covered bins
					if (x_lower < x && x < (x_upper - 1) && y_lower < y && y < (y_upper - 1)) {
						if (padding_zone) {
							this->power_maps[i][x][y] += block.power_density * power_density_scaling_padding_zone;
						}
						else {
							this->power_maps[i][x][y] += block.power_density;
						}
					}
					// else consider block power according to
					// intersection of bin and block
					else {
						// scale power density according to
						// intersection area
						intersect = Rect::determineIntersection(bin, block_offset);

						if (padding_zone) {
							this->power_maps[i][x][y] += block.power_density * power_density_scaling_padding_zone * (intersect.area / this->power_maps_bin_area);
						}
						else {
							this->power_maps[i][x][y] += block.power_density * (intersect.area / this->power_maps_bin_area);
						}
					}
				}
			}
		}
	}

	if (ThermalAnalyzer::DBG_CALLS) {
		cout << "<- ThermalAnalyzer::generatePowerMaps" << endl;
	}
}

void ThermalAnalyzer::initPowerMaps(int const& layers, double const& outline_x, double const& outline_y) {
	array<array<double,ThermalAnalyzer::POWER_MAPS_DIM>,ThermalAnalyzer::POWER_MAPS_DIM> map;
	unsigned b;
	int i;

	if (ThermalAnalyzer::DBG_CALLS) {
		cout << "-> ThermalAnalyzer::initPowerMaps(" << outline_x << ", " << outline_y << ")" << endl;
	}

	// init power maps
	this->power_maps.clear();
	this->power_maps.reserve(layers);
	// init temp map to zero
	for (auto& m : map) {
		m.fill(0.0);
	}
	// push back temp map in order to allocate power-maps memory
	for (i = 0; i < layers; i++) {
		this->power_maps.push_back(map);
	}

	// scale power map dimensions to outline of thermal map; this way the padding of
	// power maps doesn't distort the block outlines in the thermal map
	this->power_maps_dim_x = outline_x / ThermalAnalyzer::THERMAL_MAP_DIM;
	this->power_maps_dim_y = outline_y / ThermalAnalyzer::THERMAL_MAP_DIM;

	// determine offset for blocks, related to padding of power maps
	this->blocks_offset_x = this->power_maps_dim_x * ThermalAnalyzer::POWER_MAPS_PADDED_BINS;
	this->blocks_offset_y = this->power_maps_dim_y * ThermalAnalyzer::POWER_MAPS_PADDED_BINS;

	// determine max distance for blocks' upper/right boundaries to upper/right die
	// outline to be padded
	this->padding_right_boundary_blocks_distance = ThermalAnalyzer::PADDING_ZONE_BLOCKS_DISTANCE_LIMIT * outline_x;
	this->padding_upper_boundary_blocks_distance = ThermalAnalyzer::PADDING_ZONE_BLOCKS_DISTANCE_LIMIT * outline_y;

	// predetermine map bins' area and lower-left corner coordinates; note that the
	// last bin represents the upper-right coordinates for the penultimate bin
	this->power_maps_bin_area = this->power_maps_dim_x * this->power_maps_dim_y;
	for (b = 0; b <= ThermalAnalyzer::POWER_MAPS_DIM; b++) {
		this->power_maps_bins_ll_x[b] = b * this->power_maps_dim_x;
	}
	for (b = 0; b <= ThermalAnalyzer::POWER_MAPS_DIM; b++) {
		this->power_maps_bins_ll_y[b] = b * this->power_maps_dim_y;
	}

	if (ThermalAnalyzer::DBG_CALLS) {
		cout << "<- ThermalAnalyzer::initPowerMaps" << endl;
	}
}

// Determine masks for lowest layer, i.e., hottest layer.
// Based on a gaussian-like thermal impulse response fuction.
// Note that masks are centered, i.e., the value f(x=0) resides in the middle of the
// (uneven) array.
// Note that masks are 1D, sufficient for the separated convolution in
// performPowerBlurring()
void ThermalAnalyzer::initThermalMasks(int const& layers, bool const& log, MaskParameters const& parameters) {
	int i, ii;
	double scale;
	double layer_impulse_factor;
	array<double,ThermalAnalyzer::THERMAL_MASK_DIM> mask;
	int x_y;

	if (ThermalAnalyzer::DBG_CALLS) {
		cout << "-> ThermalAnalyzer::initThermalMasks(" << layers << ", " << log << ")" << endl;
	}

	if (log) {
		cout << "ThermalAnalyzer> ";
		cout << "Initializing thermals masks for power blurring ..." << endl;
	}

	// clear masks
	this->thermal_masks.clear();
	this->thermal_masks.reserve(layers);

	// determine scale factor such that mask_boundary_value is reached at the boundary
	// of the lowermost (2D) mask; based on general equation to determine x=y for
	// gauss2D such that gauss2D(x=y) = mask_boundary_value; constant spread (e.g.,
	// 1.0) is sufficient since this function fitting only requires two paramaters,
	// i.e., varying spread has no impact
	static constexpr double SPREAD = 1.0;
	scale = sqrt(SPREAD * std::log(parameters.impulse_factor / (parameters.mask_boundary_value))) / sqrt(2.0);
	// normalize factor according to half of mask dimension
	scale /=  ThermalAnalyzer::THERMAL_MASK_CENTER;

	// determine masks for lowest layer, i.e., hottest layer
	for (i = 1; i <= layers; i++) {
		// impuls factor is to be reduced notably for increasing layer count
		layer_impulse_factor = parameters.impulse_factor / pow(i, parameters.impulse_factor_scaling_exponent);

		ii = 0;
		for (x_y = -ThermalAnalyzer::THERMAL_MASK_CENTER; x_y <= ThermalAnalyzer::THERMAL_MASK_CENTER; x_y++) {
			// sqrt for impulse factor is mandatory since the mask is used for
			// separated convolution (i.e., factor will be squared in final
			// convolution result)
			mask[ii] = Math::gauss1D(x_y * scale, sqrt(layer_impulse_factor), SPREAD);
			ii++;
		}

		this->thermal_masks.push_back(mask);
	}

	if (ThermalAnalyzer::DBG) {
		// enforce fixed digit count for printing mask
		cout << fixed;
		// dump mask
		for (i = 0; i < layers; i++) {
			cout << "DBG> Thermal 1D mask for layer 0 w/ point source on layer " << i << ":" << endl;
			for (x_y = 0; x_y < ThermalAnalyzer::THERMAL_MASK_DIM; x_y++) {
				cout << this->thermal_masks[i][x_y] << ", ";
			}
			cout << endl;
		}
		// reset to default floating output
		cout.unsetf(ios_base::floatfield);

		cout << endl;
		cout << "DBG> Note that these values will be multiplied w/ each other in the final 2D mask" << endl;
	}

	if (log) {
		cout << "ThermalAnalyzer> ";
		cout << "Done" << endl << endl;
	}

	if (ThermalAnalyzer::DBG_CALLS) {
		cout << "<- ThermalAnalyzer::initThermalMasks" << endl;
	}
}
