#include "navier_solver.hpp"
#include <fstream>

using namespace mfem;
using namespace navier;

struct s_NavierContext
{
   int order = 6;
   double kin_vis = 1.0 / 100000.0;
   double t_final = 1.0;
   double dt = 1e-3;
} ctx;

void vel_shear_ic(const Vector &x, double t, Vector &u)
{
   double xi = x(0);
   double yi = x(1);

   double rho = 30.0;
   double delta = 0.05;

   if (yi <= 0.5)
   {
      u(0) = tanh(rho * (yi - 0.25));
   }
   else
   {
      u(0) = tanh(rho * (0.75 - yi));
   }

   u(1) = delta * sin(2.0 * M_PI * xi);
}

int main(int argc, char *argv[])
{
   MPI_Session mpi(argc, argv);

   int serial_refinements = 2;

   Mesh *mesh = new Mesh("../data/periodic-square.mesh");
   mesh->EnsureNodes();
   GridFunction *nodes = mesh->GetNodes();
   nodes->Neg();
   *nodes -= 1.0;
   nodes->Neg();
   *nodes /= 2.0;

   for (int i = 0; i < serial_refinements; ++i)
   {
      mesh->UniformRefinement();
   }

   if (mpi.Root())
   {
      std::cout << "Number of elements: " << mesh->GetNE() << std::endl;
   }

   auto *pmesh = new ParMesh(MPI_COMM_WORLD, *mesh);
   delete mesh;

   // Create the flow solver.
   NavierSolver flowsolver(pmesh, ctx.order, ctx.kin_vis);
   flowsolver.EnablePA(true);

   // Set the initial condition.
   // This is completely user customizeable.
   ParGridFunction *u_ic = flowsolver.GetCurrentVelocity();
   VectorFunctionCoefficient u_excoeff(pmesh->Dimension(), vel_shear_ic);
   u_ic->ProjectCoefficient(u_excoeff);

   double t = 0.0;
   double dt = ctx.dt;
   double t_final = ctx.t_final;
   bool last_step = false;

   flowsolver.Setup(dt);

   ParGridFunction *u_gf = flowsolver.GetCurrentVelocity();
   ParGridFunction *p_gf = flowsolver.GetCurrentPressure();

   ParGridFunction w_gf(*u_gf);
   flowsolver.ComputeCurl2D(*u_gf, w_gf);

   // VisItDataCollection vdc("shear_visit", pmesh);
   // vdc.SetLevelsOfDetail(ctx.order);
   // vdc.SetCycle(0);
   // vdc.SetTime(0);
   // vdc.RegisterField("velocity", u_gf);
   // vdc.RegisterField("pressure", p_gf);
   // vdc.RegisterField("vorticity", &w_gf);
   // vdc.Save();

   ParaViewDataCollection pvdc("shear", pmesh);
   pvdc.SetDataFormat(VTKFormat::BINARY32);
   pvdc.SetHighOrderOutput(true);
   pvdc.SetLevelsOfDetail(ctx.order);
   pvdc.SetCycle(0);
   pvdc.SetTime(t);
   pvdc.RegisterField("velocity", u_gf);
   pvdc.RegisterField("pressure", p_gf);
   pvdc.RegisterField("vorticity", &w_gf);
   pvdc.Save();

   for (int step = 0; !last_step; ++step)
   {
      if (t + dt >= t_final - dt / 2)
      {
         last_step = true;
      }

      flowsolver.Step(t, dt, step);

      if (step % 10 == 0)
      {
        flowsolver.ComputeCurl2D(*u_gf, w_gf);
        pvdc.SetCycle(step);
        pvdc.SetTime(t);
        pvdc.Save();
      //   vdc.SetCycle(step);
      //   vdc.SetTime(t);
      //   vdc.Save();
      }

      if (mpi.Root())
      {
         printf("%.5E %.5E\n", t, dt);
         fflush(stdout);
      }
   }

   flowsolver.PrintTimingData();

   delete pmesh;

   return 0;
}