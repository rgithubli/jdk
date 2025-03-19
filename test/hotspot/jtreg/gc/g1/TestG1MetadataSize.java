/*
 * Copyright (c) 2025, Huawei Technologies Co., Ltd. All rights reserved.
 * DO NOT ALTER OR REMOVE COPYRIGHT NOTICES OR THIS FILE HEADER.
 *
 * This code is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 only, as
 * published by the Free Software Foundation.
 *
 * This code is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
 * version 2 for more details (a copy is included in the LICENSE file that
 * accompanied this code).
 *
 * You should have received a copy of the GNU General Public License version
 * 2 along with this work; if not, write to the Free Software Foundation,
 * Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301 USA.
 *
 * Please contact Oracle, 500 Oracle Parkway, Redwood Shores, CA 94065 USA
 * or visit www.oracle.com if you need additional information or have any
 * questions.
 */

package gc.g1;

/*
 * @test TestG1MetadataSize.java
 * @bug 8350860
 * @summary Ensure G1 metadata size does not grow unreasonably.
 * Should treat JDK-8350857 as a follow up to reduce MarkStackSize
 * @requires vm.gc.G1
 * @requires vm.bits != "32"
 * @library /test/lib
 * @modules java.base/jdk.internal.misc
 *          java.management
 * @run driver gc.g1.TestG1MetadataSize
 */
import jdk.test.lib.process.OutputAnalyzer;
import jdk.test.lib.process.ProcessTools;

public class TestG1MetadataSize {
    public static void main(String[] args) throws Exception {
        testMalloc(64, 3323); // 2323k at local, increase 1m for leeway
        testMalloc(128, 3521); // 2521k at local, increase 1m for leeway
        testMalloc(256, 3915); // 2915k at local, increase 1m for leeway
        testMalloc(512, 4705); // 3705k at local, increase 1m for leeway
        testMalloc(1024, 6283); // 5283k at local, increase 1m for leeway
        testMalloc(2048, 9441); // 8441 at local, increase 1m for leeway
        testMalloc(4096, 9518); // 8518 at local, increase 1m for leeway
        testMalloc(8192, 9770); // 8770 at local, increase 1m for leeway
        testMalloc(16384, 10028); // 9028 at local, increase 1m for leeway
    }

    private static void testMalloc(int heapSizeMb, int gcMallocLimitKb) throws Exception {
        OutputAnalyzer output = ProcessTools.executeLimitedTestJava(
            String.format("-Xms%dm", heapSizeMb),
            String.format("-Xmx%dm", heapSizeMb),
            "-XX:+UseG1GC",
            "-XX:ParallelGCThreads=1",
            "-XX:ConcGCThreads=1",
            "-XX:+UnlockDiagnosticVMOptions",
            "-Xlog:nmt",
            "-XX:NativeMemoryTracking=summary",
            String.format("-XX:MallocLimit=gc:%dk", gcMallocLimitKb),
            GCTest.class.getName()
        );
        output.shouldHaveExitValue(0);
    }

    static class GCTest {
        static Object sink;
        static int ARRAY_SIZE = 100;

        public static void main(String[] args) throws Exception {
            sink = new byte[ARRAY_SIZE];
        }
    }
}
