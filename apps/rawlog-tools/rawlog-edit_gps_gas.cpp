/* +---------------------------------------------------------------------------+
   |                     Mobile Robot Programming Toolkit (MRPT)               |
   |                          http://www.mrpt.org/                             |
   |                                                                           |
   | Copyright (c) 2005-2014, Individual contributors, see AUTHORS file        |
   | See: http://www.mrpt.org/Authors - All rights reserved.                   |
   | Released under BSD License. See details in http://www.mrpt.org/License    |
   +---------------------------------------------------------------------------+ */

#include "rawlog-edit-declarations.h"

#include <mrpt/topography.h>

using namespace mrpt;
using namespace mrpt::utils;
using namespace mrpt::slam;
using namespace mrpt::system;
using namespace mrpt::rawlogtools;
using namespace mrpt::topography;
using namespace std;

// STL data must have global scope:
struct TGPSGASDataPoint
{
	double  lon,lat,alt; // degrees, degrees, meters
	uint8_t fix; // 1: standalone, 2: DGPS, 4: RTK fix, 5: RTK float, ...
	double color;
};

struct TDataPerGPSGAS
{
	map<TTimeStamp,TGPSGASDataPoint> path;
};

// ======================================================================
//		op_export_gps_gas_kml
// ======================================================================
DECLARE_OP_FUNCTION(op_export_gps_gas_kml)
{
	// A class to do this operation:
	class CRawlogProcessor_ExportGPSGAS_KML : public CRawlogProcessorOnEachObservation
	{
	protected:
		string	m_inFile;

		map<string,TDataPerGPSGAS> m_gps_paths;  // sensorLabel -> data

		const CObservationGPS* obs;
		const CObservationGasSensors* obsGas;
	public:

		CRawlogProcessor_ExportGPSGAS_KML(CFileGZInputStream &in_rawlog, TCLAP::CmdLine &cmdline, bool verbose) :
			CRawlogProcessorOnEachObservation(in_rawlog,cmdline,verbose)
		{
			getArgValue<string>(cmdline,"input",m_inFile);
		}

		// return false on any error.
		bool processOneObservation(CObservationPtr  &o)
		{
			if (IS_CLASS(o, CObservationGPS ) )
			{
				obs = CObservationGPSPtr(o).pointer();
				if (!obs->has_GGA_datum)
				{	
					obs = NULL;
					return true; // Nothing to do...
				}
			}
			else if (IS_CLASS(o, CObservationGasSensors ) )
			{
				obsGas = CObservationGasSensorsPtr(o).pointer();
			}
			else
				return true;

			// Insert the new entries:
			if(obs && obsGas)
			{
				TDataPerGPSGAS   &D = m_gps_paths[obs->sensorLabel];
				TGPSGASDataPoint &d = D.path[o->timestamp];

				d.lon = obs->GGA_datum.longitude_degrees;
				d.lat = obs->GGA_datum.latitude_degrees;
				d.alt = obs->GGA_datum.altitude_meters;
				d.fix = obs->GGA_datum.fix_quality;
				d.color = 1;
				obs = NULL;
				obsGas = NULL;
			}

			return true; // All ok
		}

		void generate_KML()
		{
			const bool save_altitude = false;

			const string outfilname = mrpt::system::fileNameChangeExtension(m_inFile,"kml");
			VERBOSE_COUT << "Writing KML file: " << outfilname << endl;

			CFileOutputStream f(outfilname);

			// Header:
			f.printf(
				"<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
				"<kml xmlns=\"http://www.opengis.net/kml/2.2\">\n"
				"<!-- File automatically generated by rawlog-edit \n"
				"      Part of the MRPT initiative - http://www.mrpt.org/ \n"
				"      Generated on %s from file '%s'  -->\n"
				"  <Document>\n"
				"    <name>GPS-GAS Paths</name>\n"
				"    <description>GPS-GAS paths from dataset '%s'</description>\n",
				mrpt::system::dateTimeLocalToString(mrpt::system::now()).c_str(),
				m_inFile.c_str(),
				m_inFile.c_str()
				);

			
			// For each GPS sensor label:			
			for (map<string,TDataPerGPSGAS>::const_iterator it=m_gps_paths.begin();it!=m_gps_paths.end();++it )
			{
				const string  &label = it->first;		//GPS Label
				const TDataPerGPSGAS &D = it->second;

				bool hasSomeRTK = false;

				f.printf(
					"    <Folder>\n"
					"      <name>%s all points</name>\n"
					"      <description>%s: All received points (for all quality levels)</description>\n"					,
					label.c_str(),
					label.c_str()					
					);

				//For each GPS point
				for (map<TTimeStamp,TGPSGASDataPoint>::const_iterator itP=D.path.begin();itP!=D.path.end();++itP)
				{
					f.printf(
					"        <Placemark>\n"
					"          <description>%s point: </description>\n"
					"          <Style>\n"
					"            <IconStyle>\n"
					"              <color></color>\n"
					"              <scale></scale>\n"
					"              <Icon><href>http://maps.google.com/mapfiles/kml/shapes/shaded_dot.png</href></Icon>\n"
					"            </IconStyle>\n"
					"          </Style>\n"
					"          <Point>\n"
					"            <coordinates></coordinates>\n"
					"          </Point>\n"
					"        </Placemark>\n"
					,
					label.c_str(),
					label.c_str(),
					int( color_idx % NCOLORS) // Color
					);
					f.printf("%s",LineString_START.c_str());

					const TGPSGASDataPoint &d = itP->second;
					// Format is: lon,lat[,alt]
					if (save_altitude)
							f.printf(" %.15f,%.15f,%.3f\n",d.lon,d.lat,d.alt);
					else 	f.printf(" %.15f,%.15f\n",d.lon,d.lat);

					if (!hasSomeRTK && d.fix==4) hasSomeRTK=true;
				}

				// end part:
				f.printf("%s",LineString_END.c_str());

				f.printf("    </Placemark>\n");

				// Do we have RTK points?
				if (hasSomeRTK)
				{
					f.printf(
						"    <Placemark>\n"
						"      <name>%s RTK only</name>\n"
						"      <description>%s: RTK fixed points only</description>\n"
						"      <styleUrl>#gpscolor%i_thick</styleUrl>\n"
						,
						label.c_str(),
						label.c_str(),
						int( color_idx % NCOLORS) // Color
						);

					f.printf(" <MultiGeometry>\n");
					f.printf("%s",LineString_START.c_str());

					TGPSGASDataPoint  last_valid;
					last_valid.lat=0;
					last_valid.lon=0;

					for (map<TTimeStamp,TGPSGASDataPoint>::const_iterator itP=D.path.begin();itP!=D.path.end();++itP)
					{
						const TGPSGASDataPoint &d = itP->second;
						if (d.fix!=4)
							continue;	// Skip this one..

						// There was a valid point?
						if (last_valid.lat!=0 && last_valid.lon!=0)
						{
							// Compute distance between points, in meters:
							//  (very rough, but fast spherical approximation):
							const double dist= 6.371e6 * DEG2RAD( ::hypot( last_valid.lon-d.lon,  last_valid.lat-d.lat ) );

							// If the distance is above a threshold, finish the line and start another one:
							if (dist>MIN_DIST_TO_SPLIT)
							{
								f.printf("%s",LineString_END.c_str());
								f.printf("%s",LineString_START.c_str());
							}

							// Format is: lon,lat[,alt]
							if (save_altitude)
									f.printf(" %.15f,%.15f,%.3f\n",d.lon,d.lat,d.alt);
							else 	f.printf(" %.15f,%.15f\n",d.lon,d.lat);

						}

						// Save last point:
						last_valid = d;

					}

					// end part:
					f.printf("%s",LineString_END.c_str());
					f.printf(" </MultiGeometry>\n");

					f.printf("    </Placemark>\n");
				}

			} // end for each sensor label

			f.printf(
				"  </Document>\n"
				"</kml>\n");
		} // end generate_KML

	}; // end CRawlogProcessor_ExportGPS_KML

	// Process
	// ---------------------------------
	CRawlogProcessor_ExportGPSGAS_KML proc(in_rawlog,cmdline,verbose);
	proc.doProcessRawlog();

	// Now that the entire rawlog is parsed, do the actual output:
	proc.generate_KML();

	// Dump statistics:
	// ---------------------------------
	VERBOSE_COUT << "Time to process file (sec)        : " << proc.m_timToParse << "\n";

}